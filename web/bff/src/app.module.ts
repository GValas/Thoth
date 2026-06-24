import { Module } from '@nestjs/common';
import { ConfigModule } from '@nestjs/config';
import { PersistenceModule } from './persistence/persistence.module';
import { EngineModule } from './engine/engine.module';
import { SchemaModule } from './schema/schema.module';
import { AuthModule } from './auth/auth.module';
import { UsersModule } from './users/users.module';
import { WorkspacesModule } from './workspaces/workspaces.module';
import { MarketDataModule } from './marketdata/marketdata.module';
import { GridModule } from './grid/grid.module';
import { HealthController } from './health.controller';

@Module({
  imports: [
    ConfigModule.forRoot({ isGlobal: true }),
    PersistenceModule,
    EngineModule, //!< @Global — EngineService everywhere
    SchemaModule, //!< @Global — SchemaService everywhere
    AuthModule, //!< registers the global JwtAuthGuard + RolesGuard
    UsersModule,
    WorkspacesModule,
    MarketDataModule,
    GridModule,
  ],
  controllers: [HealthController],
})
export class AppModule {}
