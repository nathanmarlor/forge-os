#ifndef HTTP_SERVER_H_
#define HTTP_SERVER_H_
#include <esp_http_server.h>

esp_err_t start_rest_server(void *pvParameters);

/**
 * Queue a heap-allocated string for WebSocket broadcast.
 * The string will be freed after sending. Caller must NOT free it.
 * Returns true if queued successfully, false if dropped (queue full).
 */
bool http_server_ws_send_str(char *str);

#endif