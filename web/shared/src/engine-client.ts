//! HTTP client for `thoth -server` (the unmodified C++ pricing engine).
//!
//! Verified contract (pricer/src/modes/run_server.cpp):
//!  - POST /price : body = YAML book; Content-Type application/x-yaml is MANDATORY
//!    (without it the engine caps the body at 8 KB -> 413). Optional header
//!    `X-Task-Name` selects the task (default = the book's `root:`). Response is
//!    chunked and ALWAYS HTTP 200; an error is signalled by a body starting with
//!    `error: `. Content-Type of the reply is application/x-yaml.
//!  - GET /health   -> "ok\n"
//!  - GET /progress -> "<current> <total> <active>"  (process-global, no per-request id)

import type { ProgressSnapshot } from './types.js';

const ERROR_PREFIX = 'error: ';

//! Thrown when the engine returns an `error: ...` body (mapped to HTTP 422 by the BFF).
export class EngineError extends Error {
  constructor(message: string) {
    super(message);
    this.name = 'EngineError';
  }
}

export interface EngineClientOptions {
  baseUrl: string;
  timeoutMs?: number; //!< abort a hung pricing (default 120s)
}

export class EngineClient {
  readonly baseUrl: string;
  private readonly timeoutMs: number;

  constructor(opts: EngineClientOptions) {
    this.baseUrl = opts.baseUrl.replace(/\/+$/, '');
    this.timeoutMs = opts.timeoutMs ?? 120_000;
  }

  //! Price a YAML book; resolves to the YAML result text, or throws EngineError if the
  //! engine reported a failure. taskName maps to the X-Task-Name header.
  async postPrice(bookYaml: string, taskName = 'root'): Promise<string> {
    const body = await this.fetchText('/price', {
      method: 'POST',
      headers: {
        'Content-Type': 'application/x-yaml',
        'X-Task-Name': taskName,
      },
      body: bookYaml,
    });
    if (body.startsWith(ERROR_PREFIX)) {
      throw new EngineError(body.slice(ERROR_PREFIX.length).trim());
    }
    return body;
  }

  async health(): Promise<boolean> {
    try {
      const t = await this.fetchText('/health', { method: 'GET' });
      return t.trim() === 'ok';
    } catch {
      return false;
    }
  }

  //! Parse the global progress line "<current> <total> <active>".
  async progress(): Promise<ProgressSnapshot> {
    const t = (await this.fetchText('/progress', { method: 'GET' })).trim();
    const [current, total, active] = t.split(/\s+/);
    return {
      current: Number(current) || 0,
      total: Number(total) || 0,
      active: active === '1',
    };
  }

  private async fetchText(path: string, init: RequestInit): Promise<string> {
    const ctrl = new AbortController();
    const timer = setTimeout(() => ctrl.abort(), this.timeoutMs);
    try {
      const res = await fetch(this.baseUrl + path, { ...init, signal: ctrl.signal });
      //! the engine is always 200; a non-200 means a transport/proxy problem
      if (!res.ok) {
        throw new Error(`engine ${path} HTTP ${res.status}`);
      }
      return await res.text();
    } finally {
      clearTimeout(timer);
    }
  }
}
