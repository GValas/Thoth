//! Correlation-matrix numerics shared across the BFF: the seed generator (repairing a
//! random draw), the semantic validation (SPD check before pricing) and the live
//! overlay (PD-gating a live/stored blend) all speak the same linear algebra.
//!
//! NOTE: web/spot-feed/src/correl.ts carries its own copy of jacobiEigen /
//! repairCorrelation — the spot-feed service builds from its own docker context
//! (no cross-package imports by design), so keep the two in sync when touching the
//! algorithm (same eigenvalue floor rationale, see repairCorrelation).

//! Jacobi eigenvalue decomposition of a small symmetric n×n matrix (A = V·diag(d)·Vᵀ),
//! eigenvectors as columns of V. n is small (workspace universe), so the classic
//! cyclic sweeps are plenty fast.
export function jacobiEigen(aIn: number[][]): { d: number[]; V: number[][] } {
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

//! Default eigenvalue floor for repairCorrelation. It must DOMINATE any later
//! rounding of the emitted entries: rounding to 1e-4 perturbs the spectrum by up to
//! n·5e-5 (~5e-4 for n≈10), so a tiny floor (1e-6) can leave the rounded matrix
//! indefinite — and the engine hard-rejects a non-SPD correlation. 1e-3 is
//! financially immaterial yet safely above the worst-case rounding perturbation.
export const CORREL_EIG_FLOOR = 1e-3;

//! Project a symmetric matrix onto the nearest valid correlation matrix: clip
//! eigenvalues to the floor (strictly positive-definite — what the engine requires),
//! reconstruct, then rescale to a unit diagonal.
export function repairCorrelation(m: number[][], floor = CORREL_EIG_FLOOR): number[][] {
  const n = m.length;
  const { d, V } = jacobiEigen(m);
  const dc = d.map((x) => Math.max(x, floor));
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

//! Cholesky feasibility: true iff m is strictly positive-definite (all pivots > 0).
export function isPositiveDefinite(m: number[][]): boolean {
  const n = m.length;
  const L: number[][] = Array.from({ length: n }, () => new Array<number>(n).fill(0));
  for (let i = 0; i < n; i++) {
    for (let j = 0; j <= i; j++) {
      let s = m[i][j];
      for (let k = 0; k < j; k++) s -= L[i][k] * L[j][k];
      if (i === j) {
        if (s <= 1e-12) return false;
        L[i][i] = Math.sqrt(s);
      } else {
        L[i][j] = s / L[j][j];
      }
    }
  }
  return true;
}

//! Rebuild the full symmetric matrix from the engine's lower-triangle form
//! (row-major, diagonal included, length n(n+1)/2). Returns null on a size mismatch.
export function fullFromTriangle(tri: readonly number[], n: number): number[][] | null {
  if (tri.length !== (n * (n + 1)) / 2) return null;
  const full = Array.from({ length: n }, () => new Array<number>(n).fill(0));
  let k = 0;
  for (let i = 0; i < n; i++) {
    for (let j = 0; j <= i; j++) full[i][j] = full[j][i] = tri[k++];
  }
  return full;
}

//! Rebuild the full symmetric matrix from the engine's flat row-major form (length
//! n²). Returns null on a size mismatch; symmetry is NOT checked here (the semantic
//! validation reports asymmetry with a field-level message).
export function fullFromFlat(flat: readonly number[], n: number): number[][] | null {
  if (flat.length !== n * n) return null;
  return Array.from({ length: n }, (_, i) => Array.from({ length: n }, (_, j) => flat[i * n + j]));
}

//! Flatten a full symmetric matrix back to the lower-triangle form.
export function triangleFromFull(full: readonly number[][]): number[] {
  const out: number[] = [];
  for (let i = 0; i < full.length; i++) {
    for (let j = 0; j <= i; j++) out.push(full[i][j]);
  }
  return out;
}
