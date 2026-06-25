import { Module } from '@nestjs/common';
import { WorkspacesModule } from '../workspaces/workspaces.module';
import { MarketDataModule } from '../marketdata/marketdata.module';
import { MarketFeedModule } from '../market-feed/market-feed.module';
import { InstrumentController } from './instrument.controller';
import { InstrumentService } from './instrument.service';

//! Single-instrument pricing (panels + blotter). EngineService/SchemaService are global.
@Module({
  imports: [WorkspacesModule, MarketDataModule, MarketFeedModule],
  controllers: [InstrumentController],
  providers: [InstrumentService],
})
export class InstrumentModule {}
