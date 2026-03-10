import { Component, OnInit } from '@angular/core';
import { Router } from '@angular/router';
import { ToastrService } from 'ngx-toastr';
import { interval, type Observable, shareReplay, startWith, switchMap } from 'rxjs';

import type { ISystemInfo } from '../../models/ISystemInfo';
import { SystemService } from '../services/system.service';
import { LayoutService } from './service/app.layout.service';

@Component({
    selector: 'app-menu',
    templateUrl: './app.menu.component.html'
})
export class AppMenuComponent implements OnInit {

    model: any[] = [];
    public info$!: Observable<ISystemInfo>;

    constructor(public layoutService: LayoutService,
        private systemService: SystemService,
        private toastr: ToastrService,
        private router: Router
    ) { }

    ngOnInit() {
        this.info$ = interval(10000).pipe(
            startWith(() => this.systemService.getInfo()),
            switchMap(() => this.systemService.getInfo()),
            shareReplay({ refCount: true, bufferSize: 1 })
        );

        this.model = [
            {
                label: 'Menu',
                items: [
                    { label: 'Dashboard', icon: 'pi pi-fw pi-home', routerLink: ['/'] },
                    { label: 'Swarm', icon: 'pi pi-fw pi-share-alt', routerLink: ['swarm'] },
                    { label: 'Network', icon: 'pi pi-fw pi-wifi', routerLink: ['network'] },
                    { label: 'Pool Settings', icon: 'pi pi-fw pi-server', routerLink: ['pool'] },
                    { label: 'Settings', icon: 'pi pi-fw pi-cog', routerLink: ['settings'] },
                    { label: 'Logs', icon: 'pi pi-fw pi-list', routerLink: ['logs'] },
                    { label: 'ESP Health', icon: 'pi pi-fw pi-microchip', routerLink: ['esp-health'] },
                    { label: 'Whitepaper', icon: 'pi pi-fw pi-bitcoin', command: () => window.open('/bitcoin.pdf', '_blank') },
                ]
            }
        ];
    }

    public restart() {
        this.systemService.restart().subscribe(res => {

        });
        this.toastr.success('Success!', 'BitForge restarted');
    }

    public isActive(routerLink: string[]): boolean {
        if (!routerLink) return false;
        return this.router.isActive(this.router.createUrlTree(routerLink), {
            paths: 'exact',
            queryParams: 'exact',
            fragment: 'ignored',
            matrixParams: 'ignored'
        });
    }

    public getResetReasonClass(reason: string | undefined): string {
        if (!reason) return 'reason-normal';
        const r = reason.toLowerCase();
        if (r.includes('crash') || r.includes('panic') || r.includes('brownout')) return 'reason-error';
        if (r.includes('watchdog')) return 'reason-warn';
        if (r.includes('software') || r.includes('external') || r.includes('deep sleep')) return 'reason-info';
        return 'reason-normal';
    }

    public getResetReasonTooltip(reason: string | undefined): string {
        if (!reason) return '';
        switch (reason) {
            case 'Power On':                  return 'Normal power-on or hard reset';
            case 'Software Restart':          return 'Restarted via software (API call, firmware update, etc.)';
            case 'External Pin Reset':        return 'Reset triggered via the external EN pin';
            case 'Crash / Panic':             return 'The system crashed — check for overclocking, undervoltage, or firmware bugs';
            case 'Interrupt Watchdog':        return 'An interrupt or critical section blocked for too long — possible firmware bug';
            case 'Task Watchdog (hung task)': return 'A task stopped yielding — possible I2C hang, network stall, or blocked loop';
            case 'Watchdog':                  return 'A hardware watchdog timer expired';
            case 'Brownout (low voltage)':    return 'PSU voltage dropped below the reset threshold — check your power supply';
            case 'Deep Sleep Wakeup':         return 'System woke from deep sleep';
            default:                          return 'Reset cause could not be determined';
        }
    }

    public navigateOrExecute(item: any) {
        if (item.routerLink) {
            this.router.navigate(item.routerLink);
        } else if (item.command) {
            item.command();
        }
    }
}
