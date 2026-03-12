import { Component, EventEmitter, Input, OnDestroy, OnInit, Output } from '@angular/core';
import { Subscription } from 'rxjs';
import { WebsocketService } from 'src/app/services/web-socket.service';

interface SelfTestStep {
  name: string;
  status: 'pending' | 'running' | 'passed' | 'failed';
  detail: string;
  expanded: boolean;
}

@Component({
  selector: 'app-self-test-dialog',
  styles: [`
    :host ::ng-deep .selftest-dialog.p-dialog-wrapper .p-dialog {
      background: #1e293b !important;
      border: 1px solid #334155;
      border-radius: 1rem;
      overflow: hidden;
      box-shadow: 0 25px 50px rgba(0, 0, 0, 0.5);
    }
    :host ::ng-deep .selftest-dialog .p-dialog-header {
      background: #1e293b !important;
      border-bottom: 1px solid #334155;
      padding: 1.25rem 1.5rem;
      color: #f1f5f9;
    }
    :host ::ng-deep .selftest-dialog .p-dialog-header .p-dialog-title {
      font-weight: 600;
      font-size: 1.1rem;
      letter-spacing: 0.02em;
    }
    :host ::ng-deep .selftest-dialog .p-dialog-header-icons {
      display: none !important;
    }
    :host ::ng-deep .selftest-dialog .p-dialog-content {
      background: #1e293b !important;
      padding: 1.25rem 1.5rem;
    }
    :host ::ng-deep .selftest-dialog .p-dialog-footer {
      background: #1e293b !important;
      border-top: 1px solid #334155;
      padding: 1rem 1.5rem;
    }
    :host ::ng-deep .selftest-dialog .p-dialog-footer .p-button {
      background: linear-gradient(135deg, #6366f1, #06b6d4) !important;
      border: none !important;
      border-radius: 0.5rem;
      font-weight: 500;
      transition: all 0.2s ease;
    }
    :host ::ng-deep .selftest-dialog .p-dialog-footer .p-button:hover {
      transform: translateY(-1px);
      box-shadow: 0 4px 15px rgba(99, 102, 241, 0.4);
    }
    .step-row {
      padding: 0.6rem 0.75rem;
      border-radius: 0.5rem;
      margin-bottom: 2px;
      transition: background 0.15s ease;
    }
    .step-row:hover {
      background: rgba(51, 65, 85, 0.25);
    }
    .step-row.step-failed {
      background: rgba(239, 68, 68, 0.08);
    }
    .step-badge {
      font-size: 0.65rem;
      font-weight: 700;
      letter-spacing: 0.5px;
      padding: 2px 8px;
      border-radius: 4px;
      text-transform: uppercase;
    }
    .badge-pass {
      background: rgba(16, 185, 129, 0.15);
      color: #10b981;
    }
    .badge-fail {
      background: rgba(239, 68, 68, 0.15);
      color: #ef4444;
    }
    .step-name {
      font-weight: 500;
      color: #f1f5f9;
    }
    .step-name-pending {
      color: #64748b;
    }
    .step-detail {
      font-family: 'Courier New', monospace;
      font-size: 0.78rem;
      padding: 0.5rem 0.75rem;
      margin: 4px 0 4px 2rem;
      border-radius: 0.375rem;
      background: rgba(15, 23, 42, 0.5);
      color: #94a3b8;
      border-left: 3px solid rgba(51, 65, 85, 0.5);
    }
    .step-detail.detail-fail {
      border-left-color: #ef4444;
      color: #fca5a5;
    }
    .result-box {
      padding: 1.25rem;
      border-radius: 0.75rem;
      margin-top: 1rem;
    }
    .result-pass {
      background: rgba(16, 185, 129, 0.08);
      border: 1px solid rgba(16, 185, 129, 0.2);
    }
    .result-fail {
      background: rgba(239, 68, 68, 0.08);
      border: 1px solid rgba(239, 68, 68, 0.2);
    }
    .result-text {
      font-weight: 600;
      margin-top: 0.5rem;
      margin-bottom: 0;
    }
    .init-spinner {
      font-size: 2rem;
      color: #6366f1;
    }
    .init-text {
      color: #94a3b8;
      margin-top: 0.75rem;
    }
    .chevron {
      font-size: 0.65rem;
      color: #64748b;
      transition: transform 0.15s ease;
    }
  `],
  template: `
    <p-dialog header="System Self-Test" [(visible)]="visible" [modal]="true"
              [closable]="false" [style]="{width: '520px'}"
              [styleClass]="'selftest-dialog'"
              (onHide)="onClose()">

      <div *ngIf="!started" class="text-center p-4">
        <i class="pi pi-spin pi-spinner init-spinner"></i>
        <p class="init-text">Initializing diagnostics...</p>
      </div>

      <div *ngIf="started">
        <ng-container *ngFor="let step of steps; let i = index">
          <div class="step-row flex align-items-center"
               [class.step-failed]="step.status === 'failed'"
               [style.cursor]="step.detail ? 'pointer' : 'default'"
               (click)="step.detail && toggleStep(step)">
            <i [ngClass]="getIcon(step.status)" [ngStyle]="getIconStyle(step.status)"
               style="font-size: 1.1rem; width: 1.75rem; text-align: center;"></i>
            <span class="ml-2 flex-grow-1 step-name"
                  [class.step-name-pending]="step.status === 'pending'">{{step.name}}</span>
            <span *ngIf="step.status === 'passed'" class="step-badge badge-pass">Pass</span>
            <span *ngIf="step.status === 'failed'" class="step-badge badge-fail">Fail</span>
            <i *ngIf="step.detail && (step.status === 'passed' || step.status === 'failed')"
               class="pi ml-2 chevron" [ngClass]="step.expanded ? 'pi-chevron-up' : 'pi-chevron-down'"></i>
          </div>
          <div *ngIf="step.expanded && step.detail"
               class="step-detail" [class.detail-fail]="step.status === 'failed'">
            {{step.detail}}
          </div>
        </ng-container>
      </div>

      <div *ngIf="completed" class="result-box text-center"
           [class.result-pass]="allPassed" [class.result-fail]="!allPassed">
        <i [ngClass]="allPassed ? 'pi pi-check-circle' : 'pi pi-times-circle'"
           [ngStyle]="{'color': allPassed ? '#10b981' : '#ef4444'}"
           style="font-size: 2rem;"></i>
        <p class="result-text"
           [ngStyle]="{'color': allPassed ? '#10b981' : '#ef4444'}">
          {{allPassed ? 'All Tests Passed' : 'Some Tests Failed'}}
        </p>
      </div>

      <ng-template pTemplate="footer">
        <p-button *ngIf="completed" label="Close" icon="pi pi-times" (onClick)="onClose()"></p-button>
      </ng-template>
    </p-dialog>
  `
})
export class SelfTestDialogComponent implements OnInit, OnDestroy {
  @Input() visible = false;
  @Output() visibleChange = new EventEmitter<boolean>();

  steps: SelfTestStep[] = [];
  started = false;
  completed = false;
  allPassed = false;
  private wsSub?: Subscription;

  constructor(private websocketService: WebsocketService) {}

  ngOnInit() {
    this.wsSub = this.websocketService.ws$.subscribe((message: string) => {
      let data: any;
      try {
        data = JSON.parse(message);
      } catch {
        return;
      }
      if (data?.type !== 'self_test') return;

      switch (data.event) {
        case 'started':
          this.started = true;
          this.completed = false;
          this.steps = Array.from({length: data.total_steps}, () => ({
            name: '...',
            status: 'pending' as const,
            detail: '',
            expanded: false
          }));
          if (this.steps.length > 0) {
            this.steps[0].status = 'running';
          }
          break;

        case 'step_result':
          if (data.step < this.steps.length) {
            this.steps[data.step].name = data.name;
            this.steps[data.step].status = data.passed ? 'passed' : 'failed';
            this.steps[data.step].detail = data.detail || '';
            this.steps[data.step].expanded = !data.passed;
            const next = data.step + 1;
            if (next < this.steps.length) {
              this.steps[next].status = 'running';
            }
          }
          break;

        case 'completed':
          this.completed = true;
          this.allPassed = data.passed;
          break;
      }
    });
  }

  ngOnDestroy() {
    this.wsSub?.unsubscribe();
  }

  toggleStep(step: SelfTestStep) {
    step.expanded = !step.expanded;
  }

  onClose() {
    this.visible = false;
    this.visibleChange.emit(false);
  }

  getIcon(status: string): string {
    switch (status) {
      case 'pending': return 'pi pi-circle';
      case 'running': return 'pi pi-spin pi-spinner';
      case 'passed': return 'pi pi-check-circle';
      case 'failed': return 'pi pi-times-circle';
      default: return 'pi pi-circle';
    }
  }

  getIconStyle(status: string): object {
    switch (status) {
      case 'passed': return {color: '#10b981'};
      case 'failed': return {color: '#ef4444'};
      case 'running': return {color: '#6366f1'};
      default: return {color: '#64748b'};
    }
  }
}
