//! TypeORM wiring: SQLite (better-sqlite3) at DB_PATH, synchronize for v1 (swap for
//! migrations later). Entities are auto-loaded by the feature modules via forFeature.

import { Module } from '@nestjs/common';
import { ConfigModule, ConfigService } from '@nestjs/config';
import { TypeOrmModule } from '@nestjs/typeorm';
import { ALL_ENTITIES } from './entities';

@Module({
  imports: [
    TypeOrmModule.forRootAsync({
      imports: [ConfigModule],
      inject: [ConfigService],
      useFactory: (config: ConfigService) => ({
        type: 'better-sqlite3',
        database: config.get<string>('DB_PATH', 'data/thoth.sqlite'),
        entities: ALL_ENTITIES,
        synchronize: config.get<string>('DB_SYNC', 'true') === 'true',
      }),
    }),
  ],
})
export class PersistenceModule {}
