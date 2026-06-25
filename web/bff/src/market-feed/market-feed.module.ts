import { Module } from '@nestjs/common';
import { MarketFeedController } from './market-feed.controller';
import { MarketFeedService } from './market-feed.service';

//! Live spot feed: subscribes to the spot-feed service's Redis ticks and exposes them to
//! the browser (SSE stream + latest snapshot). Self-contained; ConfigService is global.
@Module({
  controllers: [MarketFeedController],
  providers: [MarketFeedService],
  exports: [MarketFeedService], //!< GridModule overlays the live spots for live pricing
})
export class MarketFeedModule {}
