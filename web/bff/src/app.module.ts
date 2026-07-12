import { Module } from '@nestjs/common';
import { APP_GUARD } from '@nestjs/core';
import { ConfigModule } from '@nestjs/config';
import { ThrottlerGuard, ThrottlerModule } from '@nestjs/throttler';
import { PersistenceModule } from './persistence/persistence.module';
import { EngineModule } from './engine/engine.module';
import { SchemaModule } from './schema/schema.module';
import { AuthModule } from './auth/auth.module';
import { UsersModule } from './users/users.module';
import { WorkspacesModule } from './workspaces/workspaces.module';
import { MarketDataModule } from './marketdata/marketdata.module';
import { MarketFeedModule } from './market-feed/market-feed.module';
import { GridModule } from './grid/grid.module';
import { InstrumentModule } from './instrument/instrument.module';
import { HealthController } from './health.controller';

@Module({
  imports: [
    ConfigModule.forRoot({ isGlobal: true }),
    //! Global rate limiting: a sane default cap per client IP across the whole API, so an
    //! unauthenticated flood (login/refresh) or a scripted client cannot exhaust the BFF.
    //! Strict per-route limits (auth) are layered on top via @Throttle in the controller.
    ThrottlerModule.forRoot([{ ttl: 60_000, limit: 120 }]),
    PersistenceModule,
    EngineModule, //!< @Global — EngineService everywhere
    SchemaModule, //!< @Global — SchemaService everywhere
    AuthModule, //!< registers the global JwtAuthGuard + RolesGuard
    UsersModule,
    WorkspacesModule,
    MarketDataModule,
    MarketFeedModule,
    GridModule,
    InstrumentModule,
  ],
  controllers: [HealthController],
  //! ThrottlerGuard is registered here (before AuthModule's JwtAuthGuard runs) so rate
  //! limiting applies to EVERY route — including @Public login/refresh, which the JWT guard
  //! skips. Global guards run in registration order, so throttling gates the request first.
  providers: [{ provide: APP_GUARD, useClass: ThrottlerGuard }],
})
export class AppModule {}
