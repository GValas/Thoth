import { Component, OnInit, computed, effect, inject, signal } from '@angular/core';
import { MatButtonModule } from '@angular/material/button';
import { MatIconModule } from '@angular/material/icon';
import { MatSnackBar, MatSnackBarModule } from '@angular/material/snack-bar';
import { MatTooltipModule } from '@angular/material/tooltip';
import { ApiService } from '../core/api.service';
import { Workspace } from '../core/models';
import { MarketModel } from './market-model';
import { EquitiesSectionComponent } from './equities-section.component';
import { RatesSectionComponent } from './rates-section.component';
import { FxSectionComponent } from './fx-section.component';
import { CorrelationSectionComponent } from './correlation-section.component';
import { LiveSpotsService } from './live-spots.service';
import { LiveCorrelService } from './live-correl.service';

//! Market-data DASHBOARD: pick a workspace, then edit its market data in four domain areas
//! (equities · rates · fx · correlation). "Generate sample data" spawns a valid random set
//! server-side; Save PUTs the whole object set (validating on the server and surfacing any
//! errors). All four areas share one MarketModel (the canonical object list).
@Component({
  selector: 'app-market-data',
  standalone: true,
  imports: [
    MatButtonModule,
    MatIconModule,
    MatSnackBarModule,
    MatTooltipModule,
    EquitiesSectionComponent,
    RatesSectionComponent,
    FxSectionComponent,
    CorrelationSectionComponent,
  ],
  templateUrl: './market-data.component.html',
  styleUrl: './market-data.component.scss',
})
export class MarketDataComponent implements OnInit {
  private readonly api = inject(ApiService);
  private readonly snack = inject(MatSnackBar);
  readonly live = inject(LiveSpotsService);
  private readonly liveCorrel = inject(LiveCorrelService);

  readonly model = new MarketModel();
  readonly workspace = signal<Workspace | null>(null);
  readonly errors = signal<Record<string, string[]>>({});
  readonly busy = signal(false);

  //! symbols to stream live: the workspace equities plus the pivot fx pairs (the induced
  //! cross is derived in the UI, not streamed).
  readonly liveSymbols = computed(() => [
    ...this.model.equities().map((e) => e.name),
    ...this.model.forexs().map((f) => f.name),
  ]);

  constructor() {
    //! drive the live feeds from the Live toggle: on -> stream the workspace's spots + the
    //! correlation matrix, off -> stop. Re-runs when the toggle flips or the symbol set changes.
    effect(() => {
      if (this.live.enabled()) {
        void this.live.start(this.liveSymbols());
        void this.liveCorrel.start();
      } else {
        this.live.stop();
        this.liveCorrel.stop();
      }
    });
  }

  //! No workspace picker: silently use the first workspace (creating a default one if the
  //! account has none) and load its objects.
  ngOnInit(): void {
    this.api.listWorkspaces().subscribe((list) => {
      if (list.length) this.useWorkspace(list[0]);
      else this.api.createWorkspace({ name: 'Default' }).subscribe((ws) => this.useWorkspace(ws));
    });
  }

  private useWorkspace(ws: Workspace): void {
    this.workspace.set(ws);
    this.errors.set({});
    this.api.listObjects(ws.id).subscribe((objs) => {
      // the canonical book (5 stocks, USD/EUR/JPY, induced fx, correlation) is always
      // present: seed an empty workspace on first load instead of showing a blank tab.
      if (objs.length === 0) this.seed(false);
      else this.model.set(objs);
    });
  }

  generate(): void {
    this.seed(true);
  }

  //! (re)generate the canonical book server-side and load it. `confirmFirst` is true for the
  //! Generate button (it overwrites edits) and false for the silent first-load auto-seed.
  private seed(confirmFirst: boolean): void {
    const ws = this.workspace();
    if (!ws) return;
    if (
      confirmFirst &&
      !confirm(
        `Replace ${ws.name}'s market data with a fresh sample (5 stocks, 3 currencies USD/EUR/JPY, induced FX, correlation)?`,
      )
    ) {
      return;
    }
    this.busy.set(true);
    // fresh random seed each click -> a genuinely new set (the backend replaces the old one)
    this.api.seedObjects(ws.id, { seed: Math.floor(Math.random() * 1_000_000_000) }).subscribe({
      next: (objs) => {
        this.model.set(objs);
        this.errors.set({});
        this.busy.set(false);
        if (confirmFirst) this.snack.open('Sample data generated', 'OK', { duration: 2500 });
      },
      error: (e) => {
        this.busy.set(false);
        this.handleError(e);
      },
    });
  }

  save(): void {
    const ws = this.workspace();
    if (!ws) return;
    this.api.replaceObjects(ws.id, this.model.all()).subscribe({
      next: () => {
        this.errors.set({});
        this.snack.open('Saved', 'OK', { duration: 2500 });
      },
      error: (e) => this.handleError(e),
    });
  }

  errorList(): { name: string; messages: string[] }[] {
    return Object.entries(this.errors()).map(([name, messages]) => ({ name, messages }));
  }

  private handleError(e: unknown): void {
    const err = e as {
      status?: number;
      error?: { errors?: Record<string, string[]>; message?: string };
    };
    if (err.status === 400 && err.error?.errors) {
      this.errors.set(err.error.errors);
      this.snack.open('Validation failed — see errors', 'OK', { duration: 3500 });
    } else {
      this.snack.open(err.error?.message ?? 'Request failed', 'OK', { duration: 3500 });
    }
  }
}
