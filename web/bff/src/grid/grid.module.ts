import { Module } from '@nestjs/common';
import { ConfigModule } from '@nestjs/config';
import { TypeOrmModule } from '@nestjs/typeorm';
import { GridRun } from '../persistence/entities';
import { WorkspacesModule } from '../workspaces/workspaces.module';
import { MarketDataModule } from '../marketdata/marketdata.module';
import { GridController } from './grid.controller';
import { GridService } from './grid.service';

@Module({
  imports: [ConfigModule, TypeOrmModule.forFeature([GridRun]), WorkspacesModule, MarketDataModule],
  controllers: [GridController],
  providers: [GridService],
})
export class GridModule {}
