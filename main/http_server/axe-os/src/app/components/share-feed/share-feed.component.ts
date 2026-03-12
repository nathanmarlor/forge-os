import { Component, OnDestroy, OnInit } from '@angular/core';
import { Subscription } from 'rxjs';
import { WebsocketService } from 'src/app/services/web-socket.service';

type FilterMode = 'all' | 'submitted' | 'local';

interface ShareEntry {
  diff: number;
  submitted: boolean;
  timestamp: Date;
}

@Component({
  selector: 'app-share-feed',
  styles: [`
    :host {
      display: block;
      padding: 1.25rem 1rem 1.5rem 1.25rem;
      overflow: hidden;
    }
    .feed-header {
      display: flex;
      align-items: center;
      gap: 0.5rem;
      margin-bottom: 0.75rem;
    }
    .feed-title {
      color: #94a3b8;
      font-weight: 500;
      font-size: 0.8rem;
      text-transform: uppercase;
      letter-spacing: 0.05em;
      margin-right: auto;
    }
    .filter-toggle {
      display: flex;
      border: 1px solid rgba(51, 65, 85, 0.5);
      border-radius: 0.375rem;
      overflow: hidden;
    }
    .filter-btn {
      background: none;
      border: none;
      color: #64748b;
      font-size: 0.6rem;
      font-weight: 600;
      letter-spacing: 0.3px;
      text-transform: uppercase;
      padding: 0.2rem 0.45rem;
      cursor: pointer;
      transition: all 0.15s ease;
    }
    .filter-btn:not(:last-child) {
      border-right: 1px solid rgba(51, 65, 85, 0.5);
    }
    .filter-btn:hover {
      color: #94a3b8;
      background: rgba(51, 65, 85, 0.2);
    }
    .filter-btn.active {
      color: #818cf8;
      background: rgba(99, 102, 241, 0.12);
    }
    .feed-list {
      max-height: 200px;
      overflow-y: auto;
      scrollbar-width: thin;
      scrollbar-color: #334155 transparent;
    }
    .feed-list::-webkit-scrollbar { width: 4px; }
    .feed-list::-webkit-scrollbar-track { background: transparent; }
    .feed-list::-webkit-scrollbar-thumb { background: #334155; border-radius: 2px; }
    .feed-row {
      display: flex;
      align-items: center;
      padding: 0.3rem 0.5rem;
      border-radius: 0.375rem;
      margin-bottom: 1px;
      animation: fadeIn 0.3s ease;
    }
    .feed-row:hover { background: rgba(51, 65, 85, 0.25); }
    @keyframes fadeIn {
      from { opacity: 0; transform: translateX(8px); }
      to { opacity: 1; transform: translateX(0); }
    }
    .feed-time {
      color: #64748b;
      font-size: 0.7rem;
      font-family: monospace;
      width: 4.5rem;
      flex-shrink: 0;
    }
    .feed-diff {
      color: #f1f5f9;
      font-weight: 600;
      font-size: 0.85rem;
      flex: 1 1 0;
      min-width: 0;
    }
    .feed-badge {
      font-size: 0.6rem;
      font-weight: 700;
      letter-spacing: 0.5px;
      padding: 1px 6px;
      border-radius: 3px;
      text-transform: uppercase;
    }
    .badge-submitted {
      background: rgba(99, 102, 241, 0.15);
      color: #818cf8;
    }
    .badge-local {
      background: rgba(100, 116, 139, 0.15);
      color: #64748b;
    }
    .feed-empty {
      color: #64748b;
      font-size: 0.8rem;
      text-align: center;
      padding: 1.5rem 0;
    }
  `],
  template: `
    <div class="feed-header">
      <span class="feed-title">Live Share Feed</span>
      <div class="filter-toggle">
        <button class="filter-btn" [class.active]="filter === 'all'" (click)="filter = 'all'">All</button>
        <button class="filter-btn" [class.active]="filter === 'submitted'" (click)="filter = 'submitted'">Submitted</button>
        <button class="filter-btn" [class.active]="filter === 'local'" (click)="filter = 'local'">Local</button>
      </div>
    </div>
    <div class="feed-list">
      <div *ngIf="filteredShares.length === 0" class="feed-empty">
        Waiting for shares...
      </div>
      <div *ngFor="let share of filteredShares" class="feed-row">
        <span class="feed-time">{{formatTime(share.timestamp)}}</span>
        <span class="feed-diff">{{formatDiff(share.diff)}}</span>
        <span class="feed-badge" [ngClass]="share.submitted ? 'badge-submitted' : 'badge-local'">
          {{share.submitted ? 'Submitted' : 'Local'}}
        </span>
      </div>
    </div>
  `
})
export class ShareFeedComponent implements OnInit, OnDestroy {
  shares: ShareEntry[] = [];
  filter: FilterMode = 'all';
  private wsSub?: Subscription;
  private readonly MAX_ENTRIES = 50;

  constructor(private websocketService: WebsocketService) {}

  get filteredShares(): ShareEntry[] {
    if (this.filter === 'all') return this.shares;
    return this.shares.filter(s => this.filter === 'submitted' ? s.submitted : !s.submitted);
  }

  ngOnInit() {
    this.wsSub = this.websocketService.ws$.subscribe((message: string) => {
      let data: any;
      try {
        data = JSON.parse(message);
      } catch {
        return;
      }
      if (data?.type !== 'share') return;

      this.shares.unshift({
        diff: data.diff,
        submitted: data.submitted,
        timestamp: new Date()
      });

      if (this.shares.length > this.MAX_ENTRIES) {
        this.shares.pop();
      }
    });
  }

  ngOnDestroy() {
    this.wsSub?.unsubscribe();
  }

  formatTime(date: Date): string {
    return date.toLocaleTimeString([], { hour: '2-digit', minute: '2-digit', second: '2-digit' });
  }

  formatDiff(diff: number): string {
    if (diff >= 1e15) return (diff / 1e15).toFixed(2) + ' P';
    if (diff >= 1e12) return (diff / 1e12).toFixed(2) + ' T';
    if (diff >= 1e9) return (diff / 1e9).toFixed(2) + ' G';
    if (diff >= 1e6) return (diff / 1e6).toFixed(2) + ' M';
    if (diff >= 1e3) return (diff / 1e3).toFixed(2) + ' k';
    return diff.toFixed(1);
  }
}
