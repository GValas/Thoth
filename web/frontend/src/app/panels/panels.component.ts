import { Component, inject } from '@angular/core';
import { MatTabsModule } from '@angular/material/tabs';
import { InstrumentKind } from '../core/models';
import { PanelPrefillService } from './panel-prefill.service';
import { VanillaPanelComponent } from './vanilla-panel.component';
import { BarrierPanelComponent } from './barrier-panel.component';
import { VariancePanelComponent } from './variance-panel.component';
import { AutocallablePanelComponent } from './autocallable-panel.component';
import { AsianPanelComponent } from './asian-panel.component';
import { RatchetPanelComponent } from './ratchet-panel.component';
import { DigitalPanelComponent } from './digital-panel.component';

//! Pricing panels: a tab each for the vanilla, barrier, variance-swap, autocallable,
//! Asian, ratchet and digital single-product panels. Each panel quotes one hand-entered
//! instrument (live off the feed) and can push it to the global monitoring blotter —
//! keeping the Vanilla Grid for strike x maturity sweeps.
@Component({
  selector: 'app-panels',
  standalone: true,
  imports: [
    MatTabsModule,
    VanillaPanelComponent,
    BarrierPanelComponent,
    VariancePanelComponent,
    AutocallablePanelComponent,
    AsianPanelComponent,
    RatchetPanelComponent,
    DigitalPanelComponent,
  ],
  template: `
    <div class="panels-header">
      <h2 class="title">Pricing panels</h2>
    </div>
    <mat-tab-group mat-stretch-tabs="false" animationDuration="0ms" [(selectedIndex)]="selectedIndex">
      <mat-tab label="Vanilla">
        <div class="tab-body"><app-vanilla-panel></app-vanilla-panel></div>
      </mat-tab>
      <mat-tab label="Barrier">
        <div class="tab-body"><app-barrier-panel></app-barrier-panel></div>
      </mat-tab>
      <mat-tab label="Variance">
        <div class="tab-body"><app-variance-panel></app-variance-panel></div>
      </mat-tab>
      <mat-tab label="Autocallable">
        <div class="tab-body"><app-autocallable-panel></app-autocallable-panel></div>
      </mat-tab>
      <mat-tab label="Asian">
        <div class="tab-body"><app-asian-panel></app-asian-panel></div>
      </mat-tab>
      <mat-tab label="Ratchet">
        <div class="tab-body"><app-ratchet-panel></app-ratchet-panel></div>
      </mat-tab>
      <mat-tab label="Digital">
        <div class="tab-body"><app-digital-panel></app-digital-panel></div>
      </mat-tab>
    </mat-tab-group>
  `,
  styles: [
    `
      .panels-header .title {
        margin: 0 0 8px;
        font-size: 1.15rem;
      }
      .tab-body {
        padding-top: 16px;
      }
    `,
  ],
})
export class PanelsComponent {
  //! sub-tab order in the template — used to open the tab matching a blotter double-click.
  private static readonly TAB_INDEX: Record<string, number> = {
    vanilla: 0,
    barrier: 1,
    variance: 2,
    autocallable: 3,
    asian: 4,
    ratchet: 5,
    digital: 6,
  };

  //! start on the sub-tab of a pending prefill (blotter double-click), else the first tab.
  selectedIndex = ((): number => {
    const p = inject(PanelPrefillService).peek();
    return p ? (PanelsComponent.TAB_INDEX[p.kind as InstrumentKind] ?? 0) : 0;
  })();
}
