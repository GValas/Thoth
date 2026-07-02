import { describe, it, expect } from 'vitest';
import { overlayCorrelation, overlaySpots } from '../src/common/live-overlay';
import type { WsObject } from '../src/common/semantic-validation';

const eq = (name: string, spot: number): WsObject => ({
  name,
  kind: 'equity',
  payload: { spot, volatility: `${name}_vol`, currency: 'eur' },
});

describe('overlaySpots', () => {
  it('replaces quoted equity spots and leaves the rest untouched', () => {
    const out = overlaySpots(
      [eq('AAPL', 100), eq('MSFT', 200), { name: 'eur', kind: 'currency', payload: { rate: 'r' } }],
      [{ symbol: 'AAPL', price: 111.5, ts: 1 }],
    );
    expect(out.find((o) => o.name === 'AAPL')!.payload.spot).toBe(111.5);
    expect(out.find((o) => o.name === 'MSFT')!.payload.spot).toBe(200); // unquoted: stored
    expect(out.find((o) => o.name === 'eur')!.payload).toEqual({ rate: 'r' });
  });
});

describe('overlayCorrelation', () => {
  const storedTri: WsObject = {
    name: 'correl',
    kind: 'correlation_matrix',
    payload: { underlyings: ['a', 'b'], forexs: ['x'], symmetric_matrix: [1, 0.2, 1, 0.1, 0.3, 1] },
  };

  it('is a no-op without a snapshot', () => {
    expect(overlayCorrelation([storedTri], null)).toEqual([storedTri]);
  });

  it('blends live values for streamed pairs, keeps stored for the rest', () => {
    //! live universe streams a and b (not x): only the (a,b) entry updates
    const snap = { members: ['a', 'b'], matrix: [[1, 0.55], [0.55, 1]], ts: 1 };
    const out = overlayCorrelation([storedTri], snap);
    const tri = out[0].payload.symmetric_matrix as number[];
    expect(tri).toEqual([1, 0.55, 1, 0.1, 0.3, 1]); // (b,a) live; x rows stored
  });

  it('keeps the full row-major representation when the payload uses matrix', () => {
    const storedFull: WsObject = {
      name: 'correl',
      kind: 'correlation_matrix',
      payload: { underlyings: ['a', 'b'], matrix: [1, 0.2, 0.2, 1] },
    };
    const snap = { members: ['a', 'b'], matrix: [[1, -0.4], [-0.4, 1]], ts: 1 };
    const out = overlayCorrelation([storedFull], snap);
    expect(out[0].payload.matrix).toEqual([1, -0.4, -0.4, 1]);
  });

  it('falls back to the stored matrix when the blend is not positive-definite', () => {
    //! stored: rho(a,c)=0.9, rho(b,c)=-0.9; live streams only (a,b) at 0.9 — the blend
    //! (0.9, 0.9, -0.9) is jointly infeasible, so the stored matrix must be kept.
    const stored: WsObject = {
      name: 'correl',
      kind: 'correlation_matrix',
      payload: { underlyings: ['a', 'b', 'c'], symmetric_matrix: [1, 0, 1, 0.9, -0.9, 1] },
    };
    const snap = { members: ['a', 'b'], matrix: [[1, 0.9], [0.9, 1]], ts: 1 };
    const out = overlayCorrelation([stored], snap);
    expect(out[0].payload.symmetric_matrix).toEqual([1, 0, 1, 0.9, -0.9, 1]);
  });

  it('leaves non-correlation objects and unstreamed universes untouched', () => {
    const snap = { members: ['zzz'], matrix: [[1]], ts: 1 };
    const out = overlayCorrelation([storedTri, eq('a', 10)], snap);
    expect(out).toEqual([storedTri, eq('a', 10)]);
  });
});
