//! Wire types mirroring the Thoth BFF (see web/shared/src/types.ts and web/bff). Kept
//! local to the POC so it builds standalone without the pnpm/npm workspace wiring.

export type Engine = 'ana' | 'pde' | 'mcl' | 'mcl_gpu';
export type OptionType = 'call' | 'put';
export type Exercise = 'european' | 'american';

export interface Workspace {
  id: string;
  name: string;
  currency: string;
  today: string; //!< YYYY-MM-DD valuation date
}

export interface WsObject {
  name: string;
  kind: string;
  payload: Record<string, unknown>;
}

export interface GridSubmit {
  workspaceId: string;
  engine: Engine;
  underlyings: string[];
  types: OptionType[];
  strikes: number[];
  maturities: string[]; //!< YYYY-MM-DD
  indicators: string[];
  exercise?: Exercise;
  currency?: string;
}

//! One (underlying, type) result block: rows(strikes) x cols(maturities) matrices.
export interface GridMatrix {
  underlying: string;
  type: OptionType;
  currency: string;
  strikes: number[];
  maturities: string[];
  premium: number[][];
  greeks: Record<string, number[][]>;
}

export interface GridMeta {
  server?: string;
  execMs?: number;
  engineMs?: number;
  engineVersion?: string;
}

export type GridStatus = 'queued' | 'running' | 'done' | 'error';

export interface GridRun {
  id?: string;
  status: GridStatus;
  result?: { matrices: GridMatrix[]; meta?: GridMeta };
  error?: string;
}

export interface GridProgress {
  status: GridStatus;
  progress: { current: number; total: number; active: boolean } | null;
}

//! the call/put matrices for one underlying, paired for the option-chain view.
export interface OptionChain {
  underlying: string;
  currency: string;
  strikes: number[];
  maturities: string[];
  call?: GridMatrix;
  put?: GridMatrix;
}

export const GREEKS = ['delta', 'gamma', 'vega', 'rho', 'theta'] as const;
