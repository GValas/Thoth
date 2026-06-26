import { Component, OnInit, computed, inject } from '@angular/core';
import { FormsModule } from '@angular/forms';
import { MatCardModule } from '@angular/material/card';
import { MatTableModule } from '@angular/material/table';
import { MatButtonModule } from '@angular/material/button';
import { MatIconModule } from '@angular/material/icon';
import { MatFormFieldModule } from '@angular/material/form-field';
import { MatInputModule } from '@angular/material/input';
import { MatTooltipModule } from '@angular/material/tooltip';
import { PanelContextService } from '../panels/panel-context.service';
import { LiveSpotsService } from '../market-data/live-spots.service';
import { BlotterService, BlotterRow } from './blotter.service';

//! Global monitoring blotter: every product sent from a pricing panel becomes a live row.
//! Live mode re-prices the whole book off the feed on a throttle, tinting each premium green/
//! red on its last move (same charte as the option chain). Rows can be removed or cleared.
@Component({
  selector: 'app-blotter',
  standalone: true,
  imports: [
    FormsModule,
    MatCardModule,
    MatTableModule,
    MatButtonModule,
    MatIconModule,
    MatFormFieldModule,
    MatInputModule,
    MatTooltipModule,
  ],
  templateUrl: './blotter.component.html',
  styleUrl: './blotter.component.scss',
})
export class BlotterComponent implements OnInit {
  readonly b = inject(BlotterService);
  private readonly ctx = inject(PanelContextService);
  private readonly live = inject(LiveSpotsService);

  //! fixed columns + whichever Greeks appear across the rows + the row actions.
  readonly columns = computed<string[]>(() => [
    'label',
    'kind',
    'underlying',
    'spot',
    'engine',
    'premium',
    'ccy',
    ...this.b.greekColumns(),
    'status',
    'actions',
  ]);

  ngOnInit(): void {
    // ensure the shared workspace/objects are loaded (the blotter may be the first tab opened).
    this.ctx.init();
  }

  underlyingOf(row: BlotterRow): string {
    return (row.request.instrument['underlying'] as string) ?? '';
  }

  //! live spot for the row's underlying (keyed by ticker, same as the equities grid), or
  //! undefined when Live is off or the symbol isn't streamed.
  spotOf(row: BlotterRow): number | undefined {
    if (!this.live.enabled()) return undefined;
    return this.live.spots().get(this.underlyingOf(row))?.price;
  }

  //! +1/-1 on the last spot move, for the green/red tint (mirrors the equities grid).
  spotDir(row: BlotterRow): number {
    const q = this.live.spots().get(this.underlyingOf(row));
    return q ? Math.sign(q.price - q.prev) : 0;
  }

  greekOf(row: BlotterRow, g: string): number | undefined {
    return (row.greeks as Record<string, number | undefined>)[g];
  }

  fmt(v: number | null | undefined): string {
    if (v == null || !Number.isFinite(v)) return '—';
    return v.toLocaleString(undefined, { minimumFractionDigits: 4, maximumFractionDigits: 4 });
  }
}
