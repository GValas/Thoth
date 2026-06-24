//! Wire types mirroring the Thoth BFF API (see web/bff and web/shared/src/types.ts).

export type Engine = 'ana' | 'pde' | 'mcl';
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

export interface GridSubmit {
  workspaceId: string;
  engine: Engine;
  underlyings: string[];
  types: OptionType[];
  strikes: number[];
  maturities: string[]; //!< YYYY-MM-DD
  indicators: string[];
  exercise?: Exercise;
  configName?: string;
  correlationName?: string;
}

export interface GridMatrix {
  underlying: string;
  type: OptionType;
  strikes: number[];
  maturities: string[];
  premium: number[][];
  greeks: Record<string, number[][]>;
}

export type GridStatus = 'queued' | 'running' | 'done' | 'error';

export interface GridResult {
  jobId?: string;
  status: GridStatus;
  result?: { matrices: GridMatrix[] };
  error?: string;
}

export interface GridProgress {
  status: GridStatus;
  progress: { current: number; total: number; active: boolean } | null;
}

export interface UserRow {
  id: string;
  email: string;
  role: UserRole;
  enabled: boolean;
  createdAt?: string;
}
