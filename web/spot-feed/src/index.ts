//! Fake real-time equity spot feed.
//!
//! For each symbol it runs an independent geometric-Brownian-motion random walk and, on a
//! fixed cadence, publishes every new spot to Redis:
//!   - PUBLISH spots.tick  '{"symbol","price","ts"}'   (live deltas; one message per symbol)
//!   - HSET    spots:latest <symbol> '{"symbol","price","ts"}'  (snapshot for late joiners)
//! The BFF subscribes once and fans the ticks out to browsers (SSE). This is a deliberate
//! seam: swap this simulator for a real market feed and nothing downstream changes.

import { Redis } from 'ioredis';

const TICK_CHANNEL = 'spots.tick';
const LATEST_HASH = 'spots:latest';

//! the same realistic large-cap tickers the market-data seed generator samples from, so a
//! generated workspace's equities all have a live quote. Override with SYMBOLS=A,B,C.
const DEFAULT_SYMBOLS = [
  'AAPL', 'MSFT', 'GOOGL', 'AMZN', 'NVDA', 'META', 'TSLA', 'JPM', 'V', 'JNJ',
  'WMT', 'XOM', 'NESN', 'ROG', 'NOVN', 'ASML', 'SAP', 'MC', 'OR', 'SIE',
  'TM', 'SONY', '7203', 'BABA', 'TSM', 'SHEL', 'HSBA', 'AZN', 'ULVR', 'BP',
];

const env = (k: string, d: string) => process.env[k] ?? d;
const num = (k: string, d: number) => {
  const v = Number(process.env[k]);
  return Number.isFinite(v) ? v : d;
};

const REDIS_HOST = env('REDIS_HOST', 'localhost');
const REDIS_PORT = num('REDIS_PORT', 6379);
const TICK_MS = num('TICK_MS', 1000); //!< publish cadence
const DRIFT = num('SPOT_DRIFT', 0.05); //!< annualised drift μ
const VOL = num('SPOT_VOL', 0.25); //!< annualised volatility σ
const SYMBOLS = (process.env.SYMBOLS ? process.env.SYMBOLS.split(',').map((s) => s.trim()) : DEFAULT_SYMBOLS).filter(
  Boolean,
);

//! deterministic base price per symbol (a stable hash -> [40, 400]), so restarts resume at a
//! comparable level rather than jumping; the walk diverges from there.
function basePrice(symbol: string): number {
  let h = 2166136261;
  for (let i = 0; i < symbol.length; i++) {
    h = Math.imul(h ^ symbol.charCodeAt(i), 16777619);
  }
  return 40 + (Math.abs(h) % 36000) / 100; // 40.00 .. 400.00
}

//! standard normal via Box–Muller.
function gauss(): number {
  let u = 0;
  let v = 0;
  while (u === 0) u = Math.random();
  while (v === 0) v = Math.random();
  return Math.sqrt(-2 * Math.log(u)) * Math.cos(2 * Math.PI * v);
}

const SECONDS_PER_YEAR = 365 * 24 * 3600;

async function main(): Promise<void> {
  const redis = new Redis({ host: REDIS_HOST, port: REDIS_PORT, lazyConnect: false });
  redis.on('error', (e: Error) => console.error('[spot-feed] redis error:', e.message));

  const prices = new Map<string, number>(SYMBOLS.map((s) => [s, basePrice(s)]));
  const dt = (TICK_MS / 1000) / SECONDS_PER_YEAR; //!< tick length in years

  console.log(
    `[spot-feed] ${SYMBOLS.length} symbols every ${TICK_MS}ms ` +
      `(μ=${DRIFT}, σ=${VOL}) -> ${REDIS_HOST}:${REDIS_PORT} #${TICK_CHANNEL}`,
  );

  const tick = async () => {
    const ts = Date.now();
    const pipeline = redis.pipeline();
    for (const symbol of SYMBOLS) {
      const s0 = prices.get(symbol)!;
      //! GBM step: S *= exp((μ - σ²/2)·dt + σ·√dt·Z)
      const s1 = s0 * Math.exp((DRIFT - 0.5 * VOL * VOL) * dt + VOL * Math.sqrt(dt) * gauss());
      const price = Math.round(s1 * 100) / 100;
      prices.set(symbol, price);
      const payload = JSON.stringify({ symbol, price, ts });
      pipeline.publish(TICK_CHANNEL, payload);
      pipeline.hset(LATEST_HASH, symbol, payload);
    }
    try {
      await pipeline.exec();
    } catch (e) {
      console.error('[spot-feed] publish failed:', (e as Error).message);
    }
  };

  const timer = setInterval(tick, TICK_MS);

  const shutdown = () => {
    clearInterval(timer);
    redis.quit().finally(() => process.exit(0));
  };
  process.on('SIGINT', shutdown);
  process.on('SIGTERM', shutdown);
}

main().catch((e) => {
  console.error('[spot-feed] fatal:', e);
  process.exit(1);
});
