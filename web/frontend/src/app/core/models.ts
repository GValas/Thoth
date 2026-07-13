//! Wire types mirroring the Thoth BFF API (see web/bff and web/shared/src/types.ts).

export type Engine = 'ana' | 'pde' | 'mcl' | 'mcl_gpu';
export type OptionType = 'call' | 'put';
export type Exercise = 'european' | 'american';
export type UserRole = 'admin' | 'user';

export interface AuthUser {
  userId: string;
  email: string;
  role: UserRole;
}

export interface TokenResponse {
  accessToken: string;
}

export interface Health {
  status: string;
  engineReplicas: number;
  healthy: number;
}

//! JSON-Schema $def per kind, plus the kind list. Branch by kind (no anyOf).
export interface SchemaResponse {
  kinds: string[];
  defs: Record<string, JSONSchemaDef>;
}

export interface JSONSchemaDef {
  type?: string;
  title?: string;
  description?: string;
  required?: string[];
  additionalProperties?: boolean;
  properties?: Record<string, unknown>;
  [k: string]: unknown;
}

export interface Workspace {
  id: string;
  name: string;
  today: string;
  currency: string;
  ownerId?: string | null;
}

export interface CreateWorkspace {
  name: string;
  today?: string;
  currency?: string;
}

export interface UpdateWorkspace {
  name?: string;
  today?: string;
  currency?: string;
}

//! A market-data / financial object: a kind tag + free-form payload.
export interface WsObject {
  name: string;
  kind: string;
  payload: Record<string, unknown>;
}

export interface ValidateResponse {
  errors: Record<string, string[]>;
}

//! "Generate sample data" options (counts default to 5 equities / 3 currencies server-side).
export interface SeedRequest {
  equities?: number;
  currencies?: number;
  seed?: number;
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
  currency?: string; //!< contract/premium currency (defaults to the workspace currency)
  configName?: string;
  correlationName?: string;
}

export interface GridMatrix {
  underlying: string;
  type: OptionType;
  currency: string;
  strikes: number[];
  maturities: string[];
  premium: number[][];
  greeks: Record<string, number[][]>;
}

//! One underlying's call/put matrices paired up for the option-chain display: a block per
//! maturity, calls on the left and puts on the right, strikes running top-to-bottom. Either
//! side may be absent when the user priced only calls or only puts.
export interface OptionChain {
  underlying: string;
  currency: string;
  strikes: number[];
  maturities: string[];
  call?: GridMatrix;
  put?: GridMatrix;
}

//! Provenance of a computed grid: which server ran it and how long it took.
export interface GridMeta {
  server?: string; //!< engine URL (cluster master) that priced the job
  execMs?: number; //!< BFF round-trip wall clock
  engineMs?: number; //!< engine's own compute time (task_time)
  engineVersion?: string;
}

export type GridStatus = 'queued' | 'running' | 'done' | 'error';

export interface GridResult {
  jobId?: string;
  status: GridStatus;
  result?: { matrices: GridMatrix[]; meta?: GridMeta };
  error?: string;
}

export interface GridProgress {
  status: GridStatus;
  progress: { current: number; total: number; active: boolean } | null;
}

// --- single-instrument pricing (panels + blotter) ---

//! Instrument kinds the pricing panels can quote (engine `!<kind>` tags).
export type InstrumentKind =
  | 'vanilla'
  | 'barrier'
  | 'variance_swap'
  | 'autocallable'
  | 'asian'
  | 'ratchet'
  | 'digital';

//! Price one hand-entered instrument. `instrument` carries the kind's own fields
//! (underlying, strike, maturity, barrier_type, …) verbatim; `live` overlays live spots.
//! a termsheet request: the same instrument, documented instead of priced.
export interface InstrumentTermsheetRequest {
  workspaceId: string;
  kind: InstrumentKind;
  instrument: Record<string, unknown>;
  today?: string;
  title?: string;
  issuer?: string;
}

export interface InstrumentTermsheetResponse {
  termsheet: string; //!< the rendered Markdown document
  filename: string; //!< suggested download filename
}

export interface InstrumentPriceRequest {
  workspaceId: string;
  engine: Engine;
  kind: InstrumentKind;
  instrument: Record<string, unknown>;
  indicators: string[];
  currency?: string;
  today?: string;
  live?: boolean;
}

//! One instrument's flat result: premium plus whichever requested Greeks the engine wrote.
export interface InstrumentResult {
  premium: number;
  greeks: Partial<Record<'delta' | 'gamma' | 'vega' | 'rho' | 'theta', number>>;
}

export interface InstrumentPriceResponse {
  result: InstrumentResult;
  currency: string;
  meta: { server?: string; execMs: number; engineMs?: number; engineVersion?: string };
}

export interface UserRow {
  id: string;
  email: string;
  role: UserRole;
  enabled: boolean;
  createdAt?: string;
}
