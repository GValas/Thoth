import { Component, OnInit, inject, signal } from '@angular/core';
import { MatButtonModule } from '@angular/material/button';
import { MatIconModule } from '@angular/material/icon';
import { MatSnackBar, MatSnackBarModule } from '@angular/material/snack-bar';
import { ApiService } from '../core/api.service';
import { Workspace } from '../core/models';
import { MarketModel } from './market-model';
import { EquitiesSectionComponent } from './equities-section.component';
import { RatesSectionComponent } from './rates-section.component';
import { FxSectionComponent } from './fx-section.component';
import { CorrelationSectionComponent } from './correlation-section.component';

//! Market-data DASHBOARD: pick a workspace, then edit its market data in four domain areas
//! (equities · rates · fx · correlation). "Generate sample data" spawns a valid random set
//! server-side; Check validates; Save PUTs the whole object set. All four areas share one
//! MarketModel (the canonical object list).
@Component({
  selector: 'app-market-data',
  standalone: true,
  imports: [
    MatButtonModule,
    MatIconModule,
    MatSnackBarModule,
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

  readonly model = new MarketModel();
  readonly workspace = signal<Workspace | null>(null);
  readonly errors = signal<Record<string, string[]>>({});
  readonly busy = signal(false);

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
    this.api.listObjects(ws.id).subscribe((objs) => this.model.set(objs));
  }

  generate(): void {
    const ws = this.workspace();
    if (!ws) return;
    if (!confirm(`Replace ${ws.name}'s market data with a fresh random sample (5 stocks, 3 currencies)?`)) {
      return;
    }
    this.busy.set(true);
    // fresh random seed each click -> a genuinely new set (the backend replaces the old one)
    this.api.seedObjects(ws.id, { seed: Math.floor(Math.random() * 1_000_000_000) }).subscribe({
      next: (objs) => {
        this.model.set(objs);
        this.errors.set({});
        this.busy.set(false);
        this.snack.open('Sample data generated', 'OK', { duration: 2500 });
      },
      error: (e) => {
        this.busy.set(false);
        this.handleError(e);
      },
    });
  }

  check(): void {
    const ws = this.workspace();
    if (!ws) return;
    this.api.validateObjects(ws.id, this.model.all()).subscribe({
      next: (res) => {
        this.errors.set(res.errors);
        const n = Object.keys(res.errors).length;
        this.snack.open(n === 0 ? 'All objects valid' : `${n} object(s) have errors`, 'OK', {
          duration: 3000,
        });
      },
      error: (e) => this.handleError(e),
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
