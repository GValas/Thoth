import { Module } from '@nestjs/common';
import { TypeOrmModule } from '@nestjs/typeorm';
import { ObjectEntity } from '../persistence/entities';
import { MarketDataController } from './marketdata.controller';
import { MarketDataService } from './marketdata.service';

@Module({
  imports: [TypeOrmModule.forFeature([ObjectEntity])],
  controllers: [MarketDataController],
  providers: [MarketDataService],
  exports: [MarketDataService],
})
export class MarketDataModule {}
