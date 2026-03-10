import { Component } from '@angular/core'
import { interval, type Observable, shareReplay, startWith, switchMap, tap } from 'rxjs'
import { SystemService } from 'src/app/services/system.service'
import type { ISystemInfo } from 'src/models/ISystemInfo'

@Component({
  selector: 'app-esp-health',
  templateUrl: './esp-health.component.html',
  styleUrls: ['./esp-health.component.scss'],
})
export class EspHealthComponent {
  public info$!: Observable<ISystemInfo>
  public chartData?: any
  public chartOptions: any

  public dataLabels: number[] = []
  public heapFreeData: number[] = []
  public psramFreeData: number[] = []

  constructor(private systemService: SystemService) {
    this.initChart()
  }

  private initChart() {
    const textColor = '#f1f5f9'
    const textColorSecondary = '#94a3b8'
    const surfaceBorder = '#334155'

    this.chartData = {
      labels: [],
      datasets: [
        {
          type: 'line',
          label: 'Internal Heap Free',
          data: [],
          borderColor: '#10b981',
          backgroundColor: 'rgba(16, 185, 129, 0.08)',
          tension: 0.4,
          pointRadius: 1,
          pointHoverRadius: 6,
          borderWidth: 2,
          yAxisID: 'y',
          fill: 'start',
        },
        {
          type: 'line',
          label: 'PSRAM Free',
          data: [],
          borderColor: '#06b6d4',
          backgroundColor: 'rgba(6, 182, 212, 0.08)',
          tension: 0.4,
          pointRadius: 1,
          pointHoverRadius: 6,
          borderWidth: 2,
          yAxisID: 'y2',
          fill: 'start',
        },
      ],
    }

    this.chartOptions = {
      animation: false,
      maintainAspectRatio: false,
      interaction: { intersect: false, mode: 'index' },
      plugins: {
        legend: {
          labels: {
            color: textColor,
            font: { size: 14, weight: 500 },
            usePointStyle: true,
            padding: 20,
          },
        },
        tooltip: {
          backgroundColor: 'rgba(30, 41, 59, 0.95)',
          titleColor: textColor,
          bodyColor: textColor,
          borderColor: 'rgba(16, 185, 129, 0.3)',
          borderWidth: 1,
          cornerRadius: 8,
          callbacks: {
            label: (item: any) => `${item.dataset.label}: ${this.formatBytes(item.raw)}`,
          },
        },
      },
      scales: {
        x: {
          type: 'time',
          time: { unit: 'hour' },
          ticks: { color: textColorSecondary, font: { size: 12 } },
          grid: { color: surfaceBorder, drawBorder: false },
        },
        y: {
          ticks: {
            color: textColorSecondary,
            font: { size: 12 },
            callback: (v: number) => this.formatBytes(v),
          },
          grid: { color: surfaceBorder, drawBorder: false },
          beginAtZero: false,
          title: { display: true, text: 'Internal Heap', color: '#10b981', font: { size: 11 } },
        },
        y2: {
          type: 'linear',
          position: 'right',
          display: false,
          ticks: {
            color: textColorSecondary,
            font: { size: 12 },
            callback: (v: number) => this.formatBytes(v),
          },
          grid: { drawOnChartArea: false, color: surfaceBorder },
          title: { display: true, text: 'PSRAM', color: '#06b6d4', font: { size: 11 } },
        },
      },
    }

    this.info$ = interval(5000).pipe(
      startWith(() => this.systemService.getInfo()),
      switchMap(() => this.systemService.getInfo()),
      tap((info) => {
        this.dataLabels.push(new Date().getTime())
        this.heapFreeData.push(info.freeHeapInternal)
        this.psramFreeData.push(info.freeHeapSpiram)

        if (this.dataLabels.length >= 720) {
          this.dataLabels.shift()
          this.heapFreeData.shift()
          this.psramFreeData.shift()
        }

        // Show/hide PSRAM axis based on availability
        const hasPSRAM = !!info.isPSRAMAvailable
        this.chartData.datasets[1].hidden = !hasPSRAM
        this.chartOptions.scales.y2.display = hasPSRAM

        this.chartData.labels = this.dataLabels
        this.chartData.datasets[0].data = this.heapFreeData
        this.chartData.datasets[1].data = this.psramFreeData
        this.chartData = { ...this.chartData }
      }),
      shareReplay({ refCount: true, bufferSize: 1 }),
    )
  }

  public formatBytes(bytes: number): string {
    if (bytes >= 1024 * 1024) return (bytes / 1024 / 1024).toFixed(2) + ' MB'
    if (bytes >= 1024) return (bytes / 1024).toFixed(1) + ' KB'
    return bytes + ' B'
  }

  public getFragmentation(freeHeap: number, largestBlock: number | undefined): number {
    if (!largestBlock || !freeHeap) return 0
    return Math.max(0, 100 - (largestBlock / freeHeap) * 100)
  }

  public getResetReasonClass(reason: string | undefined): string {
    if (!reason) return 'reason-normal'
    const r = reason.toLowerCase()
    if (r.includes('crash') || r.includes('panic') || r.includes('brownout')) return 'reason-error'
    if (r.includes('watchdog')) return 'reason-warn'
    if (r.includes('software') || r.includes('external') || r.includes('deep sleep')) return 'reason-info'
    return 'reason-normal'
  }

  public getResetReasonDescription(reason: string | undefined): string {
    switch (reason) {
      case 'Power On':
        return 'Normal power-on or hard reset — expected when power cycling the device.'
      case 'Software Restart':
        return 'Restarted via software (API call, firmware update, etc.) — this is expected.'
      case 'External Pin Reset':
        return 'Reset triggered via the external EN pin — typically a manual reset button press.'
      case 'Crash / Panic':
        return 'The system crashed. Check for overclocking, undervoltage, or firmware bugs. Review serial logs for a stack trace.'
      case 'Interrupt Watchdog':
        return 'An interrupt or critical section blocked for too long — likely a firmware bug. Reduce overclocking or check ISR timing.'
      case 'Task Watchdog (hung task)':
        return 'A FreeRTOS task stopped yielding. Common causes: blocked I2C, network stall, or an infinite loop in a task.'
      case 'Watchdog':
        return 'A hardware watchdog timer expired — the system was unresponsive.'
      case 'Brownout (low voltage)':
        return 'PSU voltage dropped below the reset threshold. Check your power supply and cabling for voltage sag under load.'
      case 'Deep Sleep Wakeup':
        return 'System woke from deep sleep — normal for deep-sleep-enabled firmware.'
      default:
        return 'Reset cause could not be determined.'
    }
  }
}
