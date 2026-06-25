//! Live spot endpoints:
//!   GET /api/spots/latest?symbols=A,B   -> point-in-time snapshot (SpotTick[])
//!   GET /api/spots/stream?symbols=A,B   -> Server-Sent Events, one per tick
//! Both sit behind the global JwtAuthGuard. EventSource can't set an Authorization header,
//! so the Angular client streams these with fetch() + the Bearer token instead.

import { Controller, Get, Query, Sse, type MessageEvent } from '@nestjs/common';
import { Observable, map } from 'rxjs';
import { MarketFeedService, type SpotTick } from './market-feed.service';

function parseSymbols(symbols?: string): string[] | undefined {
  if (!symbols) return undefined;
  const list = symbols
    .split(',')
    .map((s) => s.trim())
    .filter(Boolean);
  return list.length ? list : undefined;
}

@Controller('spots')
export class MarketFeedController {
  constructor(private readonly feed: MarketFeedService) {}

  @Get('latest')
  latest(@Query('symbols') symbols?: string): Promise<SpotTick[]> {
    return this.feed.latest(parseSymbols(symbols));
  }

  @Sse('stream')
  stream(@Query('symbols') symbols?: string): Observable<MessageEvent> {
    const list = parseSymbols(symbols);
    const set = list ? new Set(list) : undefined;
    return this.feed.stream(set).pipe(map((tick): MessageEvent => ({ data: tick })));
  }
}
