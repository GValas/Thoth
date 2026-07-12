import 'reflect-metadata';
import { NestFactory } from '@nestjs/core';
import { ValidationPipe, Logger } from '@nestjs/common';
import { ConfigService } from '@nestjs/config';
import { DocumentBuilder, SwaggerModule } from '@nestjs/swagger';
import cookieParser from 'cookie-parser';
import helmet from 'helmet';
import { json } from 'express';
import { AppModule } from './app.module';
import { validateSecurityConfig } from './common/secret-config';

async function bootstrap(): Promise<void> {
  const app = await NestFactory.create(AppModule);

  //! Fail-fast: refuse to boot with a missing/weak/placeholder JWT secret rather than run
  //! with a guessable signing key (see secret-config.ts). Throws before we ever listen.
  validateSecurityConfig(app.get(ConfigService));

  //! Standard security headers (CSP, HSTS, no-sniff, frameguard, …) on every response.
  app.use(helmet());

  app.setGlobalPrefix('api');
  app.use(cookieParser());
  //! Cap the JSON body size: 1mb is generous for our largest legit payload (market-data
  //! save / seed) yet stops a single oversized request from exhausting memory.
  app.use(json({ limit: '1mb' }));
  app.useGlobalPipes(
    new ValidationPipe({ whitelist: true, transform: true, forbidNonWhitelisted: true }),
  );

  //! In prod the SPA is served same-origin by nginx (no CORS); in dev `ng serve`
  //! proxies /api, so the browser origin is the proxy — CORS stays off by default.
  const corsOrigin = process.env.CORS_ORIGIN;
  if (corsOrigin) {
    app.enableCors({ origin: corsOrigin.split(','), credentials: true });
  }

  //! Swagger exposes the full API surface; gate it to non-production so it is never served
  //! in prod (where it would be an unnecessary information-disclosure surface).
  if (process.env.NODE_ENV !== 'production') {
    const docs = new DocumentBuilder()
      .setTitle('Thoth dashboard API')
      .setDescription('BFF over the Thoth pricing engine')
      .setVersion('0.1')
      .addBearerAuth()
      .build();
    SwaggerModule.setup('api/docs', app, SwaggerModule.createDocument(app, docs));
  }

  const port = Number(process.env.PORT ?? 3000);
  await app.listen(port);
  new Logger('bootstrap').log(`BFF listening on :${port} (docs at /api/docs)`);
}

void bootstrap();
