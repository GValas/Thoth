//! Pricing-job queue, driver-abstracted so the same GridService runs both locally and
//! at scale. The processor leases an engine replica (which serializes/distributes the
//! actual pricing), so the queue's job is durability + backpressure, not concurrency.
//!
//!  - 'memory'  : in-process; jobs run as soon as a replica frees (EnginePool gates
//!                concurrency). No external dependency — used for dev/test here.
//!  - 'bullmq'  : Redis-backed; survives BFF restarts, worker concurrency == pool size.
//!                Selected with QUEUE_DRIVER=bullmq (+ REDIS_URL) for production.

import { Logger } from '@nestjs/common';

export type JobProcessor = (jobId: string) => Promise<void>;

export interface GridQueue {
  submit(jobId: string): Promise<void>;
  close(): Promise<void>;
}

//! In-process driver: hand the job straight to the processor. Errors are swallowed here
//! (the processor records them on the GridRun), matching BullMQ's detached semantics.
export class MemoryGridQueue implements GridQueue {
  private readonly log = new Logger('MemoryGridQueue');
  constructor(private readonly process: JobProcessor) {}

  async submit(jobId: string): Promise<void> {
    queueMicrotask(() => {
      this.process(jobId).catch((e) => this.log.error(`job ${jobId} failed: ${e}`));
    });
  }

  async close(): Promise<void> {}
}

//! Redis-backed driver. Lazily requires bullmq so a memory-driver deployment needs
//! neither the dependency resolved at runtime nor a reachable Redis. `connection` is an
//! ioredis options object ({host,port,...}); typed loosely as bullmq re-exports it.
export async function createBullMqQueue(
  connection: { host: string; port: number },
  concurrency: number,
  process: JobProcessor,
): Promise<GridQueue> {
  const { Queue, Worker } = await import('bullmq');
  // eslint-disable-next-line @typescript-eslint/no-explicit-any
  const conn = connection as any;
  const queue = new Queue('pricing', { connection: conn });
  const worker = new Worker('pricing', async (job) => process(job.data.jobId as string), {
    connection: conn,
    concurrency,
  });
  return {
    async submit(jobId: string) {
      await queue.add('grid', { jobId }, { removeOnComplete: true, removeOnFail: 100 });
    },
    async close() {
      await worker.close();
      await queue.close();
    },
  };
}
