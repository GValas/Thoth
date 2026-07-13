import { Injectable, signal } from '@angular/core';
import { Engine, InstrumentKind } from '../core/models';

//! A pending panel prefill: the booked instrument a blotter row wants to open in its
//! pricing panel (kind picks the sub-panel; engine + fields populate the form).
export interface PanelPrefill {
  kind: InstrumentKind;
  engine: Engine;
  instrument: Record<string, unknown>;
  blotterRowId?: string; //!< the originating blotter row, so re-pricing updates it in place
}

//! Hand-off channel for the "double-click a blotter row -> open it in the matching pricing
//! panel" flow. The blotter sets a pending prefill and navigates to /panels; the Panels tab
//! peeks it to select the right sub-tab, and the matching panel consumes it to fill its form.
//! Root-scoped and one-shot: consume() clears it so a later manual visit to /panels is clean.
@Injectable({ providedIn: 'root' })
export class PanelPrefillService {
  private readonly _pending = signal<PanelPrefill | null>(null);

  //! stash a contract to open in its panel (deep-copied so later blotter edits don't leak in).
  //! `blotterRowId` links it back to its blotter line so the panel re-prices that row in place.
  request(kind: InstrumentKind, engine: Engine, instrument: Record<string, unknown>, blotterRowId?: string): void {
    this._pending.set({ kind, engine, instrument: { ...instrument }, blotterRowId });
  }

  //! read the pending prefill without clearing it (the Panels tab uses this to pick the sub-tab).
  peek(): PanelPrefill | null {
    return this._pending();
  }

  //! take the pending prefill IFF it targets `kind`, clearing it (the matching panel calls this).
  consume(kind: InstrumentKind): PanelPrefill | null {
    const p = this._pending();
    if (p && p.kind === kind) {
      this._pending.set(null);
      return p;
    }
    return null;
  }
}
