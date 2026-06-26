//! Live correlation-matrix simulator.
//!
//! Evolves the off-diagonal correlations with an Ornstein–Uhlenbeck mean-reversion (each
//! entry is pulled back to a stable per-pair target plus Gaussian noise), then projects the
//! result back onto the set of valid correlation matrices (symmetric, positive-semidefinite,
//! unit diagonal, entries in [-1, 1]) by clipping the eigenvalues and renormalising the
//! diagonal. Publishing the whole universe matrix lets any workspace slice out the sub-matrix
//! for its own members — a principal sub-matrix of a valid correlation matrix is itself valid.

//! stable hash of an unordered pair -> [0, 1), so a pair's mean-reversion target is fixed
//! across restarts and independent of member order.
function pairHash(a: string, b: string): number {
  const key = a < b ? `${a}|${b}` : `${b}|${a}`;
  let h = 2166136261;
  for (let i = 0; i < key.length; i++) h = Math.imul(h ^ key.charCodeAt(i), 16777619);
  return (h >>> 0) / 4294967296;
}

//! Jacobi eigenvalue decomposition of a symmetric n×n matrix. Returns eigenvalues `d` and
//! eigenvectors as columns of `V` (A = V·diag(d)·Vᵀ). n is small (universe size), so the
//! classic cyclic Jacobi sweeps are plenty fast.
function jacobiEigen(aIn: number[][]): { d: number[]; V: number[][] } {
  const n = aIn.length;
  const a = aIn.map((r) => [...r]);
  const V: number[][] = Array.from({ length: n }, (_, i) =>
    Array.from({ length: n }, (_, j) => (i === j ? 1 : 0)),
  );
  for (let sweep = 0; sweep < 100; sweep++) {
    let off = 0;
    for (let p = 0; p < n; p++) for (let q = p + 1; q < n; q++) off += a[p][q] * a[p][q];
    if (off < 1e-14) break;
    for (let p = 0; p < n; p++) {
      for (let q = p + 1; q < n; q++) {
        if (Math.abs(a[p][q]) < 1e-18) continue;
        const theta = (a[q][q] - a[p][p]) / (2 * a[p][q]);
        const t = Math.sign(theta || 1) / (Math.abs(theta) + Math.sqrt(theta * theta + 1));
        const c = 1 / Math.sqrt(t * t + 1);
        const s = t * c;
        for (let k = 0; k < n; k++) {
          const akp = a[k][p];
          const akq = a[k][q];
          a[k][p] = c * akp - s * akq;
          a[k][q] = s * akp + c * akq;
        }
        for (let k = 0; k < n; k++) {
          const apk = a[p][k];
          const aqk = a[q][k];
          a[p][k] = c * apk - s * aqk;
          a[q][k] = s * apk + c * aqk;
        }
        for (let k = 0; k < n; k++) {
          const vkp = V[k][p];
          const vkq = V[k][q];
          V[k][p] = c * vkp - s * vkq;
          V[k][q] = s * vkp + c * vkq;
        }
      }
    }
  }
  return { d: a.map((_, i) => a[i][i]), V };
}

//! Project a symmetric matrix onto the nearest valid correlation matrix: clip eigenvalues to
//! a small positive floor (restores PSD), reconstruct, then rescale to a unit diagonal.
function repairCorrelation(m: number[][]): number[][] {
  const n = m.length;
  const { d, V } = jacobiEigen(m);
  const dc = d.map((x) => Math.max(x, 1e-8));
  const out = Array.from({ length: n }, () => Array.from({ length: n }, () => 0));
  for (let i = 0; i < n; i++) {
    for (let j = i; j < n; j++) {
      let s = 0;
      for (let k = 0; k < n; k++) s += V[i][k] * dc[k] * V[j][k];
      out[i][j] = out[j][i] = s;
    }
  }
  const inv = out.map((_, i) => 1 / Math.sqrt(out[i][i] || 1));
  for (let i = 0; i < n; i++) {
    for (let j = 0; j < n; j++) out[i][j] *= inv[i] * inv[j];
    out[i][i] = 1;
  }
  return out;
}

export interface CorrelSimOptions {
  kappa: number; //!< OU mean-reversion speed (per year)
  sigma: number; //!< OU instantaneous vol of the correlation
  dtYears: number; //!< tick length in years
  gauss: () => number; //!< standard-normal generator (shared with the price walks)
}

//! Stateful universe correlation matrix, advanced one tick at a time.
export class CorrelSim {
  readonly members: string[];
  private readonly n: number;
  private readonly target: number[][];
  private rho: number[][]; //!< current symmetric matrix, unit diagonal, always valid
  private readonly opt: CorrelSimOptions;

  constructor(members: string[], opt: CorrelSimOptions) {
    this.members = members;
    this.n = members.length;
    this.opt = opt;
    //! per-pair mean-reversion targets: equity↔equity lands in a mildly positive band, any
    //! pair touching an fx leg stays closer to zero (fx is far less correlated to single names).
    this.target = members.map((a, i) =>
      members.map((b, j) => {
        if (i === j) return 1;
        const fx = a.includes('/') || b.includes('/');
        const u = pairHash(a, b);
        return fx ? -0.15 + 0.3 * u : 0.1 + 0.45 * u; // fx: [-0.15,0.15], eq: [0.1,0.55]
      }),
    );
    this.rho = this.target.map((r) => [...r]);
  }

  //! advance the matrix one tick and return the repaired, valid matrix.
  step(): number[][] {
    const { kappa, sigma, dtYears, gauss } = this.opt;
    const sqrtDt = Math.sqrt(dtYears);
    const next = this.rho.map((r) => [...r]);
    for (let i = 0; i < this.n; i++) {
      for (let j = i + 1; j < this.n; j++) {
        const drift = kappa * (this.target[i][j] - this.rho[i][j]) * dtYears;
        let v = this.rho[i][j] + drift + sigma * sqrtDt * gauss();
        v = Math.max(-0.99, Math.min(0.99, v));
        next[i][j] = next[j][i] = v;
      }
      next[i][i] = 1;
    }
    this.rho = repairCorrelation(next);
    return this.matrix();
  }

  matrix(): number[][] {
    //! round for a tidy payload; the diagonal stays exactly 1.
    return this.rho.map((r, i) => r.map((x, j) => (i === j ? 1 : Math.round(x * 1e4) / 1e4)));
  }
}
