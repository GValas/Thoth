import { Global, Module } from '@nestjs/common';
import { ConfigModule } from '@nestjs/config';
import { EngineService } from './engine.service';

//! Global so any module can inject EngineService without re-importing.
@Global()
@Module({
  imports: [ConfigModule],
  providers: [EngineService],
  exports: [EngineService],
})
export class EngineModule {}
