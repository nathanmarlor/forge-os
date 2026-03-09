import { Component, ElementRef, Input, ViewChild } from '@angular/core';
import { MenuItem } from 'primeng/api';
import { ToastrService } from 'ngx-toastr';
import { interval, type Observable, shareReplay, startWith, switchMap } from 'rxjs';

import { LayoutService } from './service/app.layout.service';
import { SystemService } from '../services/system.service';
import type { ISystemInfo } from '../../models/ISystemInfo';

@Component({
    selector: 'app-topbar',
    templateUrl: './app.topbar.component.html'
})
export class AppTopBarComponent {

    items!: MenuItem[];
    public info$!: Observable<ISystemInfo>;

    @Input() isAPMode: boolean = false;
    
    @ViewChild('menubutton') menuButton!: ElementRef;

    @ViewChild('topbarmenubutton') topbarMenuButton!: ElementRef;

    @ViewChild('topbarmenu') menu!: ElementRef;

    constructor(
        public layoutService: LayoutService,
        private systemService: SystemService,
        private toastr: ToastrService,
    ) { 
        // Initialize system info observable for status display
        this.info$ = interval(3000).pipe(
            startWith(() => this.systemService.getInfo()),
            switchMap(() => this.systemService.getInfo()),
            shareReplay({ refCount: true, bufferSize: 1 })
        );
    }

    public restart() {
        this.systemService.restart().subscribe(() => {
            // Handle response if needed
        });
        this.toastr.success('Success!', 'BitForge restarted');
    }

    public getResetReasonClass(reason: string): string {
        if (!reason) return '';
        const r = reason.toLowerCase();
        if (r.includes('crash') || r.includes('panic') || r.includes('watchdog') || r.includes('brownout')) return 'reason-warn';
        if (r.includes('software') || r.includes('external')) return 'reason-info';
        return 'reason-normal';
    }

    public getPerformanceMode(info: ISystemInfo): { label: string, class: string } {
        const freq = info.frequency;
        const voltage = info.coreVoltage;

        if (freq === 430 && voltage === 1000) {
            return { label: 'Eco Mode', class: 'eco-mode' };
        } else if (freq === 525 && voltage === 1150) {
            return { label: 'Medium Mode', class: 'medium-mode' };
        } else if (freq === 636 && voltage === 1160) {
            return { label: 'Power Mode', class: 'power-mode' };
        } else {
            return { label: 'Custom Mode', class: 'custom-mode' };
        }
    }

}
