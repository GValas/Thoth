import { Module } from '@nestjs/common';
import { TypeOrmModule } from '@nestjs/typeorm';
import { ObjectEntity } from '../persistence/entities';
import { WorkspacesModule } from '../workspaces/workspaces.module';
import { MarketDataController } from './marketdata.controller';
import { MarketDataService } from './marketdata.service';

@Module({
  imports: [TypeOrmModule.forFeature([ObjectEntity]), WorkspacesModule],
  controllers: [MarketDataController],
  providers: [MarketDataService],
  exports: [MarketDataService],
})
export class MarketDataModule {}
