#!/usr/bin/env node
//! Thoth MCP server: exposes the C++ pricing engine to MCP clients (Claude Code /
//! Desktop, agents) as tools over stdio. The engine itself runs unmodified as
//! `thoth -server` and is reached over HTTP via @thoth/shared's EngineClient —
//! the same integration seam the web BFF uses.
//!
//! Environment:
//!   THOTH_ENGINE_URL   engine base URL         (default http://localhost:8080)
//!   THOTH_ENGINE_BIN   optional path to the thoth binary: when set and the URL
//!                      is not answering, the server SPAWNS `<bin> -server <port>`
//!                      itself and tears it down on exit — a self-contained setup.
//!   THOTH_SCHEMA_PATH  pricer JSON schema      (default resolved from the repo)
//!   MCP_TRANSPORT      'stdio' (default: launched by an MCP client) or 'http'
//!                      (a standalone service: Streamable HTTP on POST /mcp,
//!                      stateless — one server instance per request — plus a
//!                      GET /healthz probe; used by the docker-compose prod stack)
//!   MCP_PORT           HTTP port when MCP_TRANSPORT=http (default 3001)
//!   MCP_API_KEY        HTTP-mode auth: when set, every POST /mcp must carry
//!                      `Authorization: Bearer <key>` (401 otherwise). When unset the
//!                      endpoint is open and a prominent warning is logged at startup.
//!                      Ignored in stdio mode (the client owns the process).

import { readFileSync } from 'node:fs';
import { spawn, type ChildProcess } from 'node:child_process';
import { createHash, timingSafeEqual } from 'node:crypto';
import { fileURLToPath } from 'node:url';
import { resolve, dirname } from 'node:path';
import { createServer, type IncomingMessage, type ServerResponse } from 'node:http';
import { McpServer } from '@modelcontextprotocol/sdk/server/mcp.js';
import { StdioServerTransport } from '@modelcontextprotocol/sdk/server/stdio.js';
import { StreamableHTTPServerTransport } from '@modelcontextprotocol/sdk/server/streamableHttp.js';
import { z } from 'zod';
import { EngineClient, EngineError, EnginePool, loadBook, TAG_KEY, type Engine } from '@thoth/shared';
import { buildBook, parseBook, type EngineArgs, type MarketArgs } from './book.js';

//! ---- configuration -----------------------------------------------------------

const ENGINE_URL = process.env['THOTH_ENGINE_URL'] ?? 'http://localhost:8080';
const ENGINE_BIN = process.env['THOTH_ENGINE_BIN'];
const MCP_API_KEY = process.env['MCP_API_KEY'] ?? '';
const SCHEMA_PATH =
  process.env['THOTH_SCHEMA_PATH'] ??
  resolve(dirname(fileURLToPath(import.meta.url)), '../../../pricer/schema/thoth.schema.json');

const schema = JSON.parse(readFileSync(SCHEMA_PATH, 'utf8')) as {
  $defs: Record<string, unknown>;
};
const KINDS = Object.keys(schema.$defs).filter((k) => {
  const d = schema.$defs[k] as { title?: string };
  return typeof d.title === 'string' && d.title.startsWith('!'); //!< tagged object kinds only
});

//! Engine access: a plain client for health probes / self-spawn, plus a shared
//! EnginePool gating the actual pricing — the same gate the BFF uses. The pool holds
//! TWO leases on the ONE wrapped engine (same URL twice), so at most 2 MCP requests
//! are in flight against the cluster at a time; an agent burst queues here instead of
//! piling onto the engine and starving the dashboard's own pool.
const MCP_CONCURRENCY = 2;
const engine = new EngineClient({ baseUrl: ENGINE_URL });
const pool = new EnginePool(Array<string>(MCP_CONCURRENCY).fill(ENGINE_URL));

//! ---- optional self-spawned engine ---------------------------------------------

let child: ChildProcess | undefined;

//! when THOTH_ENGINE_BIN is set and nothing answers on the URL, launch the engine
//! ourselves (port taken from the URL) and reap it when the MCP server exits.
async function ensureEngine(): Promise<void> {
  if (await engine.health()) return;
  if (!ENGINE_BIN) {
    throw new Error(
      `no engine at ${ENGINE_URL} — start 'thoth -server <port>' or set THOTH_ENGINE_BIN to let the MCP server spawn it`,
    );
  }
  const port = new URL(ENGINE_URL).port || '8080';
  child = spawn(ENGINE_BIN, ['-server', port], { stdio: 'ignore' });
  child.unref();
  for (const sig of ['exit', 'SIGINT', 'SIGTERM'] as const) {
    process.on(sig, () => child?.kill());
  }
  //! wait for /health (the engine binds quickly; 5s is generous)
  for (let i = 0; i < 50; i++) {
    if (await engine.health()) return;
    await new Promise((r) => setTimeout(r, 100));
  }
  throw new Error(`spawned '${ENGINE_BIN} -server ${port}' but ${ENGINE_URL}/health never answered`);
}

//! ---- helpers -------------------------------------------------------------------

const text = (s: string) => ({ content: [{ type: 'text' as const, text: s }] });
const errText = (s: string) => ({ content: [{ type: 'text' as const, text: s }], isError: true });

//! run one synthesized-book pricing: build YAML, POST, pivot the result to JSON
async function quote(
  m: MarketArgs,
  e: EngineArgs,
  kind: string,
  fields: Record<string, unknown>,
  greeks: boolean,
) {
  const indicators = greeks
    ? ['premium', 'delta', 'gamma', 'vega', 'rho', 'theta']
    : ['premium'];
  const yaml = buildBook(m, e, kind, fields, indicators, KINDS);
  await ensureEngine();
  const reply = await pool.withEngine((c) => c.postPrice(yaml, 'pricer'));
  return parseBook(reply, indicators, KINDS);
}

//! ---- raw-book resource bounds ---------------------------------------------------

const MAX_SEQUENCE_TASKS = 50; //!< tasks per !sequence in a raw book
const MAX_BOOK_PATHS = 1_000_000; //!< MCL paths per mcl_configuration in a raw book

//! Cheap resource bounds on price_yaml_book input BEFORE it reaches the engine.
//! `paths:` only exists on !mcl_configuration in the schema, so a conservative regex
//! catches oversized path counts even in books js-yaml cannot digest. !sequence task
//! counts need structure: the book is loaded with the same tag-preserving schema the
//! builders use (every engine-valid book parses — tags only appear on top-level
//! mappings, the verified contract); if a book dodges the loader yet still declares a
//! !sequence, it is rejected rather than waved through unbounded.
function checkBookBounds(bookYaml: string): void {
  for (const m of bookYaml.matchAll(/\bpaths\s*:\s*(\d[\d_]*)/g)) {
    const n = Number(m[1]!.replaceAll('_', ''));
    if (n > MAX_BOOK_PATHS) {
      throw new Error(`book rejected: paths ${n} exceeds the MCP cap of ${MAX_BOOK_PATHS}`);
    }
  }
  if (!bookYaml.includes('!sequence')) return;
  let doc: unknown;
  try {
    doc = loadBook(bookYaml, KINDS);
  } catch (err) {
    throw new Error(`book rejected: contains !sequence but could not be parsed for bounds checking (${String(err)})`);
  }
  for (const [name, obj] of Object.entries((doc ?? {}) as Record<string, unknown>)) {
    const o = obj as { [TAG_KEY]?: string; tasks?: unknown } | null;
    if (o && o[TAG_KEY] === 'sequence' && Array.isArray(o.tasks) && o.tasks.length > MAX_SEQUENCE_TASKS) {
      throw new Error(
        `book rejected: !sequence '${name}' has ${o.tasks.length} tasks, above the MCP cap of ${MAX_SEQUENCE_TASKS}`,
      );
    }
  }
}

//! shared zod shapes (flat, LLM-friendly; percent conventions match the engine)
const marketShape = {
  today: z.string().describe('valuation date, YYYY-MM-DD'),
  spot: z.number().positive().describe('underlying spot'),
  vol_pct: z.number().positive().optional().describe('flat Black-Scholes vol in percent (e.g. 30). Exactly one of vol_pct / sabr'),
  sabr: z
    .object({
      alpha: z.number().positive(),
      beta: z.number().min(0).max(1),
      rho: z.number().gt(-1).lt(1),
      nu: z.number().min(0),
      maturities: z.array(z.number().positive()).max(20).optional().describe('pillar maturities in years, at most 20 (default flat)'),
    })
    .optional()
    .describe('SABR smile (Hagan, arbitrage-free wings) instead of a flat vol; decimals (alpha 0.3 = 30%)'),
  rate_pct: z.number().describe('flat cc zero rate in percent (e.g. 5)'),
  dividend_pct: z.number().optional().describe('flat continuous dividend yield in percent'),
  repo_pct: z.number().optional().describe('flat repo spread in percent'),
};
const engineShape = {
  engine: z.enum(['ana', 'pde', 'mcl']).describe('ana = closed form, pde = finite differences, mcl = Monte-Carlo'),
  paths: z.number().int().min(1000).max(1_000_000).optional().describe('MCL paths (default 100000, max 1000000)'),
  max_day_step: z.number().int().min(1).max(90).optional().describe('MCL diffusion step in days (default 7)'),
  greeks: z.boolean().optional().describe('also compute delta/gamma/vega/rho/theta (default false)'),
};

function splitArgs(a: Record<string, unknown>): { m: MarketArgs; e: EngineArgs; greeks: boolean } {
  return {
    m: {
      today: a['today'] as string,
      spot: a['spot'] as number,
      vol_pct: a['vol_pct'] as number | undefined,
      sabr: a['sabr'] as MarketArgs['sabr'],
      rate_pct: a['rate_pct'] as number,
      dividend_pct: a['dividend_pct'] as number | undefined,
      repo_pct: a['repo_pct'] as number | undefined,
    },
    e: {
      engine: a['engine'] as Engine,
      paths: a['paths'] as number | undefined,
      max_day_step: a['max_day_step'] as number | undefined,
    },
    greeks: (a['greeks'] as boolean | undefined) ?? false,
  };
}

//! uniform error surface: engine ERRs (bad config, unsupported product) come back
//! as tool errors with the engine's message, not as protocol failures
async function guarded<T>(fn: () => Promise<T>) {
  try {
    return text(JSON.stringify(await fn(), null, 2));
  } catch (err) {
    const msg = err instanceof EngineError ? `engine error: ${err.message}` : String(err);
    return errText(msg);
  }
}

//! ---- server ---------------------------------------------------------------------

function buildServer(): McpServer {
  const server = new McpServer({ name: 'thoth-pricing-engine', version: '0.1.0' });

  server.registerTool(
  'price_vanilla',
  {
    title: 'Price a vanilla option',
    description:
      'Price a European or American vanilla option (call/put) on a single equity with the Thoth ' +
      'C++ engine. Choose the engine: ana (closed form / Hagan SABR), pde (Crank-Nicolson grid, ' +
      'required for American with dividends) or mcl (Monte-Carlo, Sobol). Flat market synthesized ' +
      'from the arguments; optional SABR smile. Returns premium (and Greeks) as JSON.',
    inputSchema: {
      ...marketShape,
      ...engineShape,
      strike: z.number().positive(),
      maturity: z.string().describe('maturity date, YYYY-MM-DD'),
      type: z.enum(['call', 'put']),
      exercise: z.enum(['european', 'american']).optional().describe('default european'),
    },
  },
  async (a) => {
    const { m, e, greeks } = splitArgs(a);
    return guarded(() =>
      quote(m, e, 'vanilla', {
        strike: a.strike,
        maturity: a.maturity,
        type: a.type,
        exercise: a.exercise ?? 'european',
        is_absolute_strike: true,
      }, greeks),
    );
  },
);

  server.registerTool(
  'price_barrier',
  {
    title: 'Price a barrier option',
    description:
      'Price a knock-in / knock-out barrier option (up&out, up&in, down&out, down&in) with ' +
      'continuous or discrete monitoring. ana = Reiner-Rubinstein closed form (continuous only), ' +
      'pde / mcl handle both. Returns premium (and Greeks) as JSON.',
    inputSchema: {
      ...marketShape,
      ...engineShape,
      strike: z.number().positive(),
      maturity: z.string().describe('maturity date, YYYY-MM-DD'),
      type: z.enum(['call', 'put']),
      barrier_type: z.enum(['up&out', 'up&in', 'down&out', 'down&in']),
      barrier_up_level: z.number().positive().optional().describe('required for up& types'),
      barrier_down_level: z.number().positive().optional().describe('required for down& types'),
      monitoring_period_days: z
        .number()
        .int()
        .min(0)
        .optional()
        .describe('days between barrier observations; 0/absent = continuous monitoring'),
    },
  },
  async (a) => {
    const { m, e, greeks } = splitArgs(a);
    const discrete = (a.monitoring_period_days ?? 0) > 0;
    return guarded(() =>
      quote(m, e, 'barrier', {
        strike: a.strike,
        maturity: a.maturity,
        type: a.type,
        barrier_type: a.barrier_type,
        barrier_monitoring_type: discrete ? 'discrete_monitoring' : 'continuous_monitoring',
        ...(a.barrier_up_level !== undefined ? { barrier_up_level: a.barrier_up_level } : {}),
        ...(a.barrier_down_level !== undefined ? { barrier_down_level: a.barrier_down_level } : {}),
        ...(discrete ? { monitoring_period_days: a.monitoring_period_days } : {}),
        is_absolute_strike: true,
      }, greeks),
    );
  },
);

  server.registerTool(
  'price_variance_swap',
  {
    title: 'Price a variance swap',
    description:
      'Price a variance swap paying notional * (realized_variance - strike_variance). ' +
      'ana = static replication of the option strip (smile-aware under SABR), pde = ' +
      'accumulated-variance grid, mcl = realized variance of the simulated path. Optional ' +
      'discrete fixing schedule (observation_period_days). Returns the PV as JSON.',
    inputSchema: {
      ...marketShape,
      ...engineShape,
      maturity: z.string().describe('maturity date, YYYY-MM-DD'),
      volatility_strike_pct: z.number().min(0).describe('variance strike quoted as a vol, in percent'),
      notional: z.number().describe('variance notional (cash per unit of annualized variance)'),
      observation_period_days: z
        .number()
        .int()
        .min(0)
        .optional()
        .describe('days between realized-variance fixings; 0/absent = continuous observation'),
    },
  },
  async (a) => {
    const { m, e, greeks } = splitArgs(a);
    return guarded(() =>
      quote(m, e, 'variance_swap', {
        maturity: a.maturity,
        volatility_strike: a.volatility_strike_pct,
        notional: a.notional,
        ...(a.observation_period_days ? { observation_period_days: a.observation_period_days } : {}),
      }, greeks),
    );
  },
);

  server.registerTool(
  'price_yaml_book',
  {
    title: 'Price a raw Thoth YAML book',
    description:
      'Full engine access: POST a complete Thoth YAML configuration (multi-asset books, quanto / ' +
      'composite / basket underlyings, Heston / Bates stochastic vol, term-structured curves, ' +
      'discrete dividends, !sequence task matrices, model-parameter vega_<param> Greeks, ...) and ' +
      'get the raw result YAML back. Use get_config_schema to author the config; the samples in ' +
      'pricer/samples/ are good templates. Resource bounds: at most 50 tasks per !sequence and ' +
      '1000000 Monte-Carlo paths per mcl_configuration.',
    inputSchema: {
      yaml: z.string().max(262144).describe('the complete YAML configuration (root: names the task)'),
      task_name: z.string().optional().describe("task to execute (default: the config's root)"),
    },
  },
  async (a) =>
    guarded(async () => {
      checkBookBounds(a.yaml);
      await ensureEngine();
      //! task_name omitted -> no X-Task-Name header -> the engine uses the book's root:
      return await pool.withEngine((c) => c.postPrice(a.yaml, a.task_name));
    }),
);

  server.registerTool(
  'get_config_schema',
  {
    title: 'Inspect the Thoth configuration schema',
    description:
      'The JSON Schema of the YAML configuration the engine prices. Without arguments: the list ' +
      'of object kinds (tags). With kind: that object definition (fields, requireds, enums) — ' +
      'use it to author price_yaml_book configs.',
    inputSchema: {
      kind: z.string().optional().describe("an object kind, e.g. 'vanilla', 'heston_volatility'"),
    },
  },
  async (a) =>
    guarded(async () => {
      if (!a.kind) return { kinds: KINDS };
      const def = schema.$defs[a.kind];
      if (!def) throw new Error(`unknown kind '${a.kind}' — valid kinds: ${KINDS.join(', ')}`);
      return def;
    }),
);

  server.registerTool(
  'engine_health',
  {
    title: 'Check the pricing engine',
    description: 'Reachability and latency of the wrapped thoth -server instance.',
    inputSchema: {},
  },
  async () =>
    guarded(async () => {
      //! the engine URL is internal topology — report reachability + latency only
      const t0 = Date.now();
      const healthy = await engine.health();
      return {
        healthy,
        latency_ms: healthy ? Date.now() - t0 : null,
        spawnable: Boolean(ENGINE_BIN),
      };
    }),
);

  return server;
}

//! ---- main ------------------------------------------------------------------------

//! Bearer auth for the HTTP transport. When MCP_API_KEY is set, every /mcp POST must
//! carry `Authorization: Bearer <key>`. The compare is constant-time: both sides are
//! SHA-256 hashed first so timingSafeEqual always sees equal-length buffers and no
//! length or prefix information leaks. In HTTP mode the key is guaranteed non-empty
//! (startup fails closed otherwise), so this only accepts a matching bearer token.
function authorized(req: IncomingMessage): boolean {
  if (!MCP_API_KEY) return true;
  const header = req.headers.authorization ?? '';
  const token = header.startsWith('Bearer ') ? header.slice('Bearer '.length) : '';
  const digest = (s: string) => createHash('sha256').update(s).digest();
  return timingSafeEqual(digest(token), digest(MCP_API_KEY));
}

if ((process.env['MCP_TRANSPORT'] ?? 'stdio') === 'http') {
  //! standalone service mode (prod stack): stateless Streamable HTTP — a fresh
  //! server+transport pair per POST (no session state to keep), torn down with the
  //! response. GET /healthz answers the compose health probe.
  const port = Number(process.env['MCP_PORT'] ?? 3001);
  //! FAIL-CLOSED: the HTTP transport is network-exposed (nginx proxies it on the public
  //! dashboard port), so refuse to start without a key rather than silently serving an
  //! unauthenticated pricing endpoint. stdio mode is unaffected — it has no network surface
  //! and the client owns the process, so it never reaches this branch.
  if (!MCP_API_KEY) {
    console.error(
      'thoth-mcp: FATAL — MCP_API_KEY is unset in HTTP transport mode. POST /mcp is ' +
        'network-exposed (nginx proxies it on the public dashboard port); refusing to start ' +
        'an unauthenticated endpoint. Set MCP_API_KEY and register clients with an ' +
        "'Authorization: Bearer <key>' header.",
    );
    process.exit(1);
  }
  //! Cap the accumulated request body: JSON.parse over an unbounded stream is a trivial
  //! memory-exhaustion vector. 1 MB is far above any legitimate MCP JSON-RPC call.
  const MAX_BODY_BYTES = 1024 * 1024;
  const http = createServer(async (req: IncomingMessage, res: ServerResponse) => {
    if (req.method === 'GET' && req.url === '/healthz') {
      const healthy = await engine.health();
      res.writeHead(healthy ? 200 : 503, { 'content-type': 'application/json' });
      //! no engine URL here: the probe is publicly reachable via nginx (/mcp/healthz)
      res.end(JSON.stringify({ healthy }));
      return;
    }
    if (req.method !== 'POST' || !(req.url === '/mcp' || req.url === '/')) {
      res.writeHead(405, { 'content-type': 'application/json' });
      res.end(JSON.stringify({ jsonrpc: '2.0', error: { code: -32000, message: 'POST /mcp only (stateless server)' }, id: null }));
      return;
    }
    if (!authorized(req)) {
      res.writeHead(401, { 'content-type': 'application/json', 'www-authenticate': 'Bearer' });
      res.end(JSON.stringify({ jsonrpc: '2.0', error: { code: -32000, message: "unauthorized: this server requires 'Authorization: Bearer <MCP_API_KEY>'" }, id: null }));
      return;
    }
    try {
      const chunks: Buffer[] = [];
      let received = 0;
      for await (const c of req) {
        const chunk = c as Buffer;
        received += chunk.length;
        if (received > MAX_BODY_BYTES) {
          //! reject oversized bodies before they accumulate; 413 Payload Too Large.
          req.destroy();
          if (!res.headersSent) {
            res.writeHead(413, { 'content-type': 'application/json' });
            res.end(JSON.stringify({ jsonrpc: '2.0', error: { code: -32000, message: 'request body too large (max 1 MB)' }, id: null }));
          }
          return;
        }
        chunks.push(chunk);
      }
      const body = JSON.parse(Buffer.concat(chunks).toString('utf8'));
      const server = buildServer();
      const transport = new StreamableHTTPServerTransport({ sessionIdGenerator: undefined });
      res.on('close', () => {
        void transport.close();
        void server.close();
      });
      await server.connect(transport);
      await transport.handleRequest(req, res, body);
    } catch (err) {
      if (!res.headersSent) {
        res.writeHead(500, { 'content-type': 'application/json' });
        res.end(JSON.stringify({ jsonrpc: '2.0', error: { code: -32603, message: String(err) }, id: null }));
      }
    }
  });
  http.listen(port, () => {
    console.error(`thoth-mcp: Streamable HTTP on :${port}/mcp — ${KINDS.length} config kinds (engine: ${ENGINE_URL})`);
  });
} else {
  const transport = new StdioServerTransport();
  await buildServer().connect(transport);
  //! stderr only: stdout carries the MCP protocol
  console.error(`thoth-mcp: serving ${KINDS.length} config kinds over stdio (engine: ${ENGINE_URL})`);
}
