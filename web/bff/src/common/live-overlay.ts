//! Live market overlays for the synchronous "Live" pricing paths (grid + single
//! instrument): the latest feed values are laid over the workspace's STORED objects
//! just before the book is built, so the priced market matches what the Market Data
//! screen shows live. Pure functions (no Nest) so they're easy to test.

import type { SpotTick, CorrelSnapshot } from '../market-feed/market-feed.service';
import type { WsObject } from './semantic-validation';
import { fullFromFlat, fullFromTriangle, isPositiveDefinite, triangleFromFull } from './correlation-math';

//! Overlay the latest live spots onto quoted equities. Equities the feed does not
//! quote keep their stored spot, so a partial feed still prices.
export function overlaySpots(objects: ReadonlyArray<WsObject>, ticks: SpotTick[]): WsObject[] {
  const px = new Map(ticks.map((t) => [t.symbol, t.price]));
  return objects.map((o) =>
    o.kind === 'equity' && px.has(o.name)
      ? { ...o, payload: { ...o.payload, spot: px.get(o.name) as number } }
      : o,
  );
}

//! Overlay the live universe correlation matrix onto every stored correlation_matrix:
//! each pair whose BOTH members stream gets the live value, every other entry keeps
//! its stored value. Mixing live and stored entries can break positive-definiteness
//! (each source is valid, their blend need not be), and the engine hard-rejects a
//! non-SPD matrix — so the blended matrix is Cholesky-gated and the stored one kept
//! on failure. Payloads keep their original representation (matrix / symmetric_matrix).
export function overlayCorrelation(
  objects: ReadonlyArray<WsObject>,
  snap: CorrelSnapshot | null,
): WsObject[] {
  if (!snap) return [...objects];
  const index = new Map(snap.members.map((m, i) => [m, i]));

  return objects.map((o) => {
    if (o.kind !== 'correlation_matrix') return o;

    const members = [
      ...(Array.isArray(o.payload.underlyings) ? (o.payload.underlyings as string[]) : []),
      ...(Array.isArray(o.payload.forexs) ? (o.payload.forexs as string[]) : []),
    ];
    const n = members.length;
    if (n === 0) return o;

    //! reconstruct the stored full matrix from either representation
    const flat = o.payload.matrix;
    const tri = o.payload.symmetric_matrix;
    let full: number[][] | null = null;
    if (Array.isArray(flat)) {
      full = fullFromFlat(flat as number[], n);
    } else if (Array.isArray(tri)) {
      full = fullFromTriangle(tri as number[], n);
    }
    if (!full) return o; //!< malformed payload: leave it to validation / the engine

    //! blend: live value where both members stream (e.g. a Heston `<eq>_var`
    //! pseudo-member is never streamed, so its rows stay stored)
    let touched = false;
    for (let i = 0; i < n; i++) {
      const li = index.get(members[i]);
      if (li === undefined) continue;
      for (let j = 0; j < i; j++) {
        const lj = index.get(members[j]);
        if (lj === undefined) continue;
        const v = snap.matrix[li]?.[lj];
        if (typeof v === 'number' && Number.isFinite(v)) {
          full[i][j] = full[j][i] = v;
          touched = true;
        }
      }
    }
    if (!touched) return o;

    //! safety gate: never hand the engine a non-SPD blend — fall back to stored
    if (!isPositiveDefinite(full)) return o;

    if (Array.isArray(flat)) {
      const out: number[] = [];
      for (let i = 0; i < n; i++) for (let j = 0; j < n; j++) out.push(full[i][j]);
      return { ...o, payload: { ...o.payload, matrix: out } };
    }
    return { ...o, payload: { ...o.payload, symmetric_matrix: triangleFromFull(full) } };
  });
}
