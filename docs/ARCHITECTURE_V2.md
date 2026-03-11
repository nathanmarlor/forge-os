# Forge-OS Architecture V2

## Overview

This document describes the redesigned architecture for forge-os. The goal is to
replace the monolithic `GlobalState` struct with a modular, message-passing
architecture while preserving the existing mining pipeline and hardware support.

## Design Principles

1. **Modules own their state** - No shared mutable struct. Each module has private
   state and exposes a query API.
2. **Message passing over shared memory** - FreeRTOS queues for inter-task events.
3. **Explicit interfaces** - Each module declares what it produces and consumes.
4. **Object pools over malloc** - Pre-allocated ring buffers for hot-path objects.
5. **State machines for protocols** - Stratum connection as explicit states.
6. **Layered ASIC driver** - Transport / Protocol / Driver separation.

---

## Module Map

```
┌─────────────────────────────────────────────────────────────────┐
│                        app_main()                               │
│  Boot → HW init → NVS → Config → Peripherals → Module init     │
└───────────────────────────┬─────────────────────────────────────┘
                            │ creates modules, wires event queues
                            ▼
┌──────────────────────────────────────────────────────────────────┐
│                      Event Bus (event_bus.h)                     │
│  Lightweight pub/sub: publishers post typed events,              │
│  subscribers receive on their own FreeRTOS queue.                │
│  Events: MINING_NOTIFY, JOB_READY, NONCE_FOUND, SHARE_RESULT,   │
│          HASHRATE_UPDATE, TEMP_UPDATE, CONFIG_CHANGED, etc.      │
└──────────────────────────────────────────────────────────────────┘
        │           │           │            │            │
        ▼           ▼           ▼            ▼            ▼
   ┌─────────┐ ┌─────────┐ ┌─────────┐ ┌──────────┐ ┌──────────┐
   │ Stratum │ │   Job   │ │  ASIC   │ │  Power   │ │   HTTP   │
   │ Module  │ │ Builder │ │ Module  │ │  Module  │ │  Module  │
   └─────────┘ └─────────┘ └─────────┘ └──────────┘ └──────────┘
```

---

## Module Specifications

### 1. Event Bus (`components/event_bus/`)

The backbone of inter-module communication. Replaces direct GlobalState access.

```c
// Event types
typedef enum {
    EVT_MINING_NOTIFY,       // New work from pool
    EVT_JOB_READY,           // Job built, ready for ASIC
    EVT_NONCE_FOUND,         // ASIC found a nonce
    EVT_SHARE_SUBMIT,        // Share to send to pool
    EVT_SHARE_RESULT,        // Pool accepted/rejected
    EVT_REGISTER_READ,       // ASIC register data
    EVT_HASHRATE_UPDATE,     // New hashrate calculation
    EVT_TEMP_UPDATE,         // Temperature reading
    EVT_POWER_UPDATE,        // Power/voltage reading
    EVT_CONFIG_CHANGED,      // NVS config was modified
    EVT_POOL_STATE,          // Connection state change
    EVT_ABANDON_WORK,        // Clear queues, new block
    EVT_CLOCK_SYNC,          // NTP time from pool
} event_type_t;

// Event envelope
typedef struct {
    event_type_t type;
    uint32_t timestamp_ms;
    union {
        mining_notify_event_t   mining_notify;
        nonce_found_event_t     nonce_found;
        share_submit_event_t    share_submit;
        share_result_event_t    share_result;
        register_read_event_t   register_read;
        hashrate_update_event_t hashrate;
        temp_update_event_t     temperature;
        power_update_event_t    power;
        config_changed_event_t  config;
        pool_state_event_t      pool_state;
    } data;
} event_t;

// API
esp_err_t event_bus_init(void);
esp_err_t event_bus_subscribe(event_type_t type, QueueHandle_t subscriber_queue);
esp_err_t event_bus_publish(const event_t *event);
```

**Implementation:** Array of subscriber lists per event type. `publish()` copies the
event into each subscriber's queue. Non-blocking (xQueueSend with 0 timeout) - if a
subscriber is full, the event is dropped with a warning log.

**Memory:** Static allocation. Max 8 subscribers per event type, max 16 event types.

---

### 2. Stratum Module (`main/modules/stratum/`)

Replaces: `stratum_task.c`, heartbeat logic, parts of `system.c`

**Owns:**
- TCP socket lifecycle
- Pool connection state machine
- Stratum JSON-RPC parsing
- Share submission queue
- Response time tracking
- Primary/fallback pool switching

**State Machine:**
```
DISCONNECTED ──► CONNECTING ──► CONFIGURING ──► SUBSCRIBING ──► AUTHORIZING ──► RUNNING
     ▲                                                                            │
     │                                                                            │
     └──────────────── ERROR ◄────────────────────────────────────────────────────┘
                         │
                         ▼
                   FALLBACK_CONNECTING ──► ... ──► FALLBACK_RUNNING
                         │                              │
                         └──── HEARTBEAT_PROBING ◄──────┘
```

**Publishes:** `EVT_MINING_NOTIFY`, `EVT_SHARE_RESULT`, `EVT_POOL_STATE`, `EVT_ABANDON_WORK`, `EVT_CLOCK_SYNC`
**Subscribes:** `EVT_SHARE_SUBMIT`, `EVT_CONFIG_CHANGED`

```c
typedef struct {
    stratum_state_t state;
    int sock;
    int send_uid;
    // Pool config (primary + fallback)
    pool_config_t primary;
    pool_config_t fallback;
    bool using_fallback;
    int retry_count;
    // Extranonce
    char *extranonce_str;
    int extranonce_2_len;
    uint32_t version_mask;
    uint32_t stratum_difficulty;
    // Response time tracking
    response_time_tracker_t rtt;
} stratum_module_t;

esp_err_t stratum_module_init(stratum_module_t *module, const stratum_config_t *config);
void stratum_module_task(void *pvParameters);

// Query API (called from HTTP handler)
stratum_state_t stratum_get_state(const stratum_module_t *module);
const response_time_tracker_t *stratum_get_rtt(const stratum_module_t *module);
bool stratum_is_using_fallback(const stratum_module_t *module);
```

---

### 3. Job Builder Module (`main/modules/job_builder/`)

Replaces: `create_jobs_task.c`

**Owns:**
- Mining notification → bm_job conversion
- Merkle root calculation
- Extranonce2 generation
- Job object pool

**Publishes:** `EVT_JOB_READY`
**Subscribes:** `EVT_MINING_NOTIFY`, `EVT_ABANDON_WORK`

```c
#define JOB_POOL_SIZE 16

typedef struct {
    bm_job jobs[JOB_POOL_SIZE];       // Pre-allocated ring
    char jobid_buf[JOB_POOL_SIZE][64];
    char extranonce2_buf[JOB_POOL_SIZE][32];
    uint8_t head;
    uint8_t count;
    // Current work context
    uint32_t version_mask;
    char *extranonce_str;
    int extranonce_2_len;
} job_builder_module_t;
```

The job pool eliminates per-job malloc/free. Jobs are indices into the ring buffer.
When a job slot is recycled, its strings are overwritten in-place.

---

### 4. ASIC Module (`main/modules/asic/`)

Replaces: `asic_task.c`, `asic_result_task.c`

**Owns:**
- Job dispatch to UART
- Result collection from UART
- Active job tracking (128 slots)
- Nonce validation
- Register read scheduling

**Two internal tasks:**
- **Dispatch task** (priority 10): Dequeues jobs, sends to ASIC via UART
- **Result task** (priority 15): Polls UART, validates nonces, publishes events

**Publishes:** `EVT_NONCE_FOUND`, `EVT_SHARE_SUBMIT`, `EVT_REGISTER_READ`
**Subscribes:** `EVT_JOB_READY`, `EVT_ABANDON_WORK`

```c
typedef struct {
    // Active job table
    bm_job *active_jobs[128];
    uint8_t valid_jobs[128];
    pthread_mutex_t jobs_lock;
    // Dispatch
    SemaphoreHandle_t dispatch_semaphore;
    double job_interval_ms;
    // Config
    uint32_t asic_difficulty;
    uint32_t stratum_difficulty;
} asic_module_t;
```

---

### 5. Stats Module (`main/modules/stats/`)

Replaces: `hashrate_monitor_task.c`, `system.c` stats tracking, `cpu_monitor_task.c`

**Owns:**
- Register-based hashrate calculation (per-ASIC, per-domain)
- Error rate tracking
- Share accepted/rejected counters
- Best difficulty tracking
- CPU load monitoring

**Publishes:** `EVT_HASHRATE_UPDATE`
**Subscribes:** `EVT_REGISTER_READ`, `EVT_SHARE_RESULT`, `EVT_NONCE_FOUND`

```c
typedef struct {
    // Hashrate
    measurement_t *total_measurement;
    measurement_t **domain_measurements;
    measurement_t *error_measurement;
    float hashrate_ema;
    float error_percentage;
    int error_count;
    // Shares
    uint64_t shares_accepted;
    uint64_t shares_rejected;
    RejectedReasonStat rejected_reasons[10];
    int rejected_reason_count;
    // Best diff
    uint64_t best_nonce_diff;
    uint64_t best_session_nonce_diff;
    char best_diff_string[10];
    char best_session_diff_string[10];
    bool found_block;
    // CPU
    float cpu0_percent;
    float cpu1_percent;
    // Lock for reads from HTTP
    pthread_mutex_t lock;
} stats_module_t;

// Query API
float stats_get_hashrate(const stats_module_t *module);
void stats_get_share_counts(const stats_module_t *module, uint64_t *accepted, uint64_t *rejected);
```

---

### 6. Power Module (`main/modules/power/`)

Replaces: `power_management_task.c`

**Owns:**
- Temperature polling (EMC2101 per ASIC, TPS546 VR)
- Fan control (auto/manual)
- Voltage regulation
- Frequency scaling
- Overheat protection
- INA260 power monitoring

**Publishes:** `EVT_TEMP_UPDATE`, `EVT_POWER_UPDATE`
**Subscribes:** `EVT_CONFIG_CHANGED`, `EVT_HASHRATE_UPDATE`

```c
typedef struct {
    // Readings
    float chip_temp[6];
    float chip_temp_avg;
    float vr_temp;
    float voltage;
    float power;
    float current;
    uint16_t fan_perc;
    uint16_t fan_rpm[2];
    // Control
    float frequency_value;
    float frequency_multiplier;
    uint16_t overheat_mode;
} power_module_t;
```

---

### 7. Config Module (`main/modules/config/`)

Replaces: `nvs_config.c/h`, `nvs_device.c/h`, config parts of `system.c`

**Owns:**
- NVS read/write
- Config change notification
- Runtime config cache (avoids repeated NVS reads)

**Publishes:** `EVT_CONFIG_CHANGED`

```c
typedef struct {
    // Pool
    pool_config_t primary_pool;
    pool_config_t fallback_pool;
    // Hardware
    uint16_t asic_frequency;
    uint16_t asic_voltage;
    // Fan
    uint16_t fan_speed;
    bool auto_fan;
    uint16_t fan_target_temp;
    uint16_t fan_min_speed;
    // Misc
    uint16_t overheat_mode;
    bool overclock_enabled;
} config_module_t;

esp_err_t config_init(config_module_t *module);
esp_err_t config_set_u16(config_module_t *module, const char *key, uint16_t value);
uint16_t config_get_u16(const config_module_t *module, const char *key, uint16_t default_val);
```

When a value is changed via `config_set_*`, the module writes to NVS and publishes
`EVT_CONFIG_CHANGED` with the key. Subscribers (power, stratum) react without restart.

---

### 8. HTTP Module (`main/modules/http/`)

Replaces: `http_server.c`

**Owns:**
- REST API endpoints
- SPIFFS static file serving
- WebSocket log streaming
- CORS handling

**Reads from:** All modules via their query APIs (stats, power, stratum, config)
**Publishes:** `EVT_CONFIG_CHANGED` (via config module when settings are POSTed)

The HTTP module holds pointers to each module's public struct and calls their
query functions. No direct field access to private state.

```c
typedef struct {
    stats_module_t *stats;
    power_module_t *power;
    stratum_module_t *stratum;
    config_module_t *config;
    asic_module_t *asic;
    // Device info (immutable after boot)
    const char *device_model_str;
    const char *asic_model_str;
    uint8_t asic_count;
    uint16_t small_core_count;
} http_module_t;
```

---

### 9. ASIC Driver Layers (`components/asic/`)

Current `bm1370.c` (600+ lines mixing transport, protocol, and driver) splits into:

```
components/asic/
├── include/
│   ├── asic_transport.h    // UART framing, CRC, send/recv
│   ├── asic_protocol.h     // Chip-specific register maps, init sequences
│   ├── asic_driver.h       // High-level: send_job(), read_result(), read_registers()
│   └── bm1370_regs.h       // BM1370 register definitions
├── asic_transport.c         // UART + CRC (shared across chip types)
├── bm1370_protocol.c        // BM1370-specific command encoding
├── bm1370_driver.c          // BM1370 driver implementation
└── asic_driver.c            // Dispatch layer (switch on chip type)
```

**Transport layer** is reusable for BM1368, BM1366, etc.
**Protocol layer** encodes chip-specific commands.
**Driver layer** provides the clean API used by the ASIC module.

---

## Application Context

Replaces `GlobalState`. Holds module pointers, not module state.

```c
typedef struct {
    // Device identity (immutable after boot)
    DeviceModel device_model;
    char *device_model_str;
    AsicModel asic_model;
    char *asic_model_str;
    int board_version;
    uint8_t asic_count;
    uint16_t small_core_count;
    uint32_t asic_difficulty;
    double asic_job_frequency_ms;
    bool psram_available;

    // Module instances
    config_module_t config;
    stratum_module_t stratum;
    job_builder_module_t job_builder;
    asic_module_t asic;
    stats_module_t stats;
    power_module_t power;
    http_module_t http;
} app_context_t;
```

Each module is initialized with its dependencies explicitly passed. No module
reaches into another module's private fields.

---

## Boot Sequence (new)

```c
void app_main(void) {
    // 1. Hardware init (same as current)
    i2c_init();
    adc_init();
    nvs_init();

    // 2. Config module loads all NVS values
    config_init(&ctx.config);
    detect_device_model(&ctx);

    // 3. Event bus
    event_bus_init();

    // 4. Peripheral init (LEDs, thermal, voltage, fan)
    system_init_peripherals(&ctx);

    // 5. WiFi
    wifi_init_and_connect(&ctx);

    // 6. Module init (order matters for dependencies)
    power_module_init(&ctx.power, &ctx.config);
    stats_module_init(&ctx.stats, ctx.asic_count);
    stratum_module_init(&ctx.stratum, &ctx.config);
    job_builder_module_init(&ctx.job_builder);
    asic_module_init(&ctx.asic, &ctx);

    // 7. HTTP server (needs pointers to all modules)
    http_module_init(&ctx.http, &ctx);

    // 8. ASIC hardware init
    serial_init();
    asic_driver_init(ctx.asic_model, ctx.config.asic_frequency, ctx.asic_count);

    // 9. Start tasks
    xTaskCreate(stratum_module_task,     "stratum",    8192, &ctx.stratum,    5,  NULL);
    xTaskCreate(job_builder_task,        "job_builder", 8192, &ctx.job_builder, 10, NULL);
    xTaskCreate(asic_dispatch_task,      "asic_tx",    8192, &ctx.asic,       10, NULL);
    xTaskCreate(asic_result_task,        "asic_rx",    8192, &ctx.asic,       15, NULL);
    xTaskCreate(stats_task,              "stats",      4096, &ctx.stats,       5, NULL);
    xTaskCreate(power_module_task,       "power",      8192, &ctx.power,      10, NULL);
}
```

---

## Migration Strategy

The refactor is done incrementally. Each phase compiles and runs.

### Phase 1: Foundation (event bus + app context)
- Create `event_bus` component
- Create `app_context_t` that wraps the existing `GlobalState`
- All existing code still works through the wrapper

### Phase 2: Extract Config Module
- Move NVS logic into `config_module_t`
- Add `EVT_CONFIG_CHANGED` publishing
- Power task subscribes instead of polling NVS every 2s

### Phase 3: Extract Stats Module
- Move hashrate monitor + share counters + best diff into `stats_module_t`
- Subscribe to events instead of direct callbacks

### Phase 4: Extract Power Module
- Move thermal/fan/voltage logic into `power_module_t`
- Publish `EVT_TEMP_UPDATE` / `EVT_POWER_UPDATE`

### Phase 5: Extract Stratum Module (state machine)
- Rewrite stratum_task as explicit state machine
- Move RTT tracking, pool switching, heartbeat into module
- Publish/subscribe for mining notifications and shares

### Phase 6: Extract Job Builder + ASIC Module
- Job object pool replaces malloc
- ASIC module owns the active_jobs table
- Event-driven dispatch

### Phase 7: Refactor HTTP Module
- Replace direct GlobalState reads with module query APIs
- Clean up the 1500-line http_server.c

### Phase 8: Layer the ASIC Driver
- Split bm1370.c into transport/protocol/driver
- Make it straightforward to add new chip support

---

## Task List

### Phase 1: Foundation
- [ ] 1.1 Create `components/event_bus/` with event types, pub/sub, init
- [ ] 1.2 Create `main/app_context.h` with `app_context_t`
- [ ] 1.3 Wire `app_context_t` into `app_main()` alongside existing `GlobalState`
- [ ] 1.4 Verify compilation and boot

### Phase 2: Config Module
- [ ] 2.1 Create `main/modules/config/` with config_module_t
- [ ] 2.2 Move NVS read/write into config module
- [ ] 2.3 Add config change notification via event bus
- [ ] 2.4 Update power_management_task to subscribe to config changes
- [ ] 2.5 Update HTTP POST handlers to use config module

### Phase 3: Stats Module
- [ ] 3.1 Create `main/modules/stats/` with stats_module_t
- [ ] 3.2 Move hashrate monitor logic into stats module
- [ ] 3.3 Move share counting and best diff into stats module
- [ ] 3.4 Move CPU monitor into stats module
- [ ] 3.5 Subscribe to EVT_REGISTER_READ, EVT_SHARE_RESULT, EVT_NONCE_FOUND
- [ ] 3.6 Add query API for HTTP server

### Phase 4: Power Module
- [ ] 4.1 Create `main/modules/power/` with power_module_t
- [ ] 4.2 Move thermal polling and fan control
- [ ] 4.3 Move voltage regulation and frequency scaling
- [ ] 4.4 Publish EVT_TEMP_UPDATE and EVT_POWER_UPDATE
- [ ] 4.5 Subscribe to EVT_CONFIG_CHANGED for runtime frequency/voltage changes

### Phase 5: Stratum Module
- [ ] 5.1 Create `main/modules/stratum/` with state machine
- [ ] 5.2 Define stratum_state_t enum and transition table
- [ ] 5.3 Move connection logic into state handlers
- [ ] 5.4 Move heartbeat into HEARTBEAT_PROBING state
- [ ] 5.5 Move RTT tracking into module
- [ ] 5.6 Publish EVT_MINING_NOTIFY, EVT_SHARE_RESULT, EVT_POOL_STATE
- [ ] 5.7 Subscribe to EVT_SHARE_SUBMIT

### Phase 6: Job Builder + ASIC Module
- [ ] 6.1 Create `main/modules/job_builder/` with job object pool
- [ ] 6.2 Implement pre-allocated job ring buffer
- [ ] 6.3 Subscribe to EVT_MINING_NOTIFY, publish EVT_JOB_READY
- [ ] 6.4 Create `main/modules/asic/` with asic_module_t
- [ ] 6.5 Move active_jobs table and nonce validation
- [ ] 6.6 Split into dispatch + result tasks
- [ ] 6.7 Publish EVT_NONCE_FOUND, EVT_SHARE_SUBMIT, EVT_REGISTER_READ

### Phase 7: HTTP Module
- [ ] 7.1 Create `main/modules/http/` with http_module_t
- [ ] 7.2 Replace direct GlobalState reads with module query APIs
- [ ] 7.3 Split GET_system_info into smaller handlers per module

### Phase 8: ASIC Driver Layers
- [ ] 8.1 Create `asic_transport.h/c` - UART framing and CRC
- [ ] 8.2 Create `bm1370_regs.h` - register definitions
- [ ] 8.3 Create `bm1370_protocol.c` - command encoding
- [ ] 8.4 Create `bm1370_driver.c` - high-level driver API
- [ ] 8.5 Update asic_module to use new driver API

---

## Memory Budget

| Component        | Estimated RAM | Notes                              |
|-----------------|---------------|-------------------------------------|
| Event bus        | ~2 KB         | Static arrays, no dynamic alloc     |
| Config cache     | ~512 B        | Cached NVS values                   |
| Job pool         | ~2 KB         | 16 × bm_job (128 bytes each)        |
| Stats module     | ~1 KB         | Measurement arrays (SPIRAM if avail) |
| Module structs   | ~2 KB         | All module state combined            |
| FreeRTOS queues  | ~4 KB         | Event subscriber queues              |
| **Total**        | **~12 KB**    | vs current ~8 KB for GlobalState     |

The 4 KB overhead is acceptable given the ESP32-S3's ~400 KB internal + 2-8 MB SPIRAM.

---

## What Does NOT Change

- FreeRTOS as the RTOS
- ESP-IDF as the framework
- cJSON for stratum parsing
- SPIFFS for web UI
- Existing hardware drivers (INA260, TPS546, EMC2101, etc.)
- The mining algorithm and nonce validation logic
- WiFi AP+STA dual mode with timeout
- The AxeOS web frontend (no API changes, just internal refactoring)
