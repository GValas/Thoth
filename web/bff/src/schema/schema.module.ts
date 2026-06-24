import { Global, Module } from '@nestjs/common';
import { ConfigModule } from '@nestjs/config';
import { SchemaController } from './schema.controller';
import { SchemaService } from './schema.service';

//! Global: SchemaService (kinds list) is needed by the YAML/grid/marketdata layers.
@Global()
@Module({
  imports: [ConfigModule],
  controllers: [SchemaController],
  providers: [SchemaService],
  exports: [SchemaService],
})
export class SchemaModule {}
