import { describe, it, expect } from 'vitest';
import { EnginePool } from '../src/engine-pool.js';

describe('EnginePool semaphore', () => {
  it('leases up to size replicas, queues the rest, and wakes waiters on release', async () => {
    const pool = new EnginePool(['http://a:8080', 'http://b:8080']); // size 2
    expect(pool.size).toBe(2);

    const l1 = await pool.acquire();
    const l2 = await pool.acquire();

    // third acquire must block until a lease is released
    let third: Awaited<ReturnType<typeof pool.acquire>> | undefined;
    const pending = pool.acquire().then((l) => (third = l));
    await tick();
    expect(third).toBeUndefined();

    l1.release();
    await pending;
    expect(third).toBeDefined();
    expect(third!.client).toBe(l1.client); // freed replica handed straight to the waiter

    // double release is a no-op (must not over-fill the pool)
    l1.release();
    third!.release();
    l2.release();
  });

  it('withEngine releases even when the body throws', async () => {
    const pool = new EnginePool(['http://a:8080']); // size 1
    await expect(pool.withEngine(async () => { throw new Error('boom'); })).rejects.toThrow('boom');
    // if release happened, a subsequent acquire resolves immediately
    const l = await pool.acquire();
    expect(l.client).toBeDefined();
    l.release();
  });

  it('rejects an empty pool', () => {
    expect(() => new EnginePool([])).toThrow();
  });
});

const tick = () => new Promise((r) => setTimeout(r, 10));
