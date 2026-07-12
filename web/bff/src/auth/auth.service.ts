//! JWT auth: short-lived access token + rotating refresh token. The refresh token is
//! a signed JWT whose HASH is stored on the user, so a refresh both verifies the token
//! AND that it is the current (un-rotated, un-revoked) one. Logout clears the hash.

import { createHash } from 'node:crypto';
import { Injectable, OnModuleInit, Logger, UnauthorizedException } from '@nestjs/common';
import { ConfigService } from '@nestjs/config';
import { JwtService } from '@nestjs/jwt';
import { InjectRepository } from '@nestjs/typeorm';
import { Repository } from 'typeorm';
import * as bcrypt from 'bcryptjs';
import { User } from '../persistence/entities';
import type { JwtPayload } from './jwt.strategy';
import { requiredSecret } from '../common/secret-config';

export interface Tokens {
  accessToken: string;
  refreshToken: string;
}

//! bcrypt only reads the first 72 BYTES of its input, and two refresh JWTs of the same
//! user share their first 72 bytes (constant header + the payload's leading sub/email —
//! iat/exp sit at the END of the payload), so bcrypt-ing the raw token would make every
//! token of a user compare equal and turn the rotation check into a no-op. Digesting to
//! a fixed 64-char sha256 hex first keeps the whole token significant.
function tokenDigest(token: string): string {
  return createHash('sha256').update(token).digest('hex');
}

@Injectable()
export class AuthService implements OnModuleInit {
  private readonly log = new Logger(AuthService.name);

  constructor(
    @InjectRepository(User) private readonly users: Repository<User>,
    private readonly jwt: JwtService,
    private readonly config: ConfigService,
  ) {}

  //! Seed an initial admin from ADMIN_EMAIL/ADMIN_PASSWORD if the users table is empty.
  async onModuleInit(): Promise<void> {
    if ((await this.users.count()) > 0) return;
    const email = this.config.get<string>('ADMIN_EMAIL');
    const password = this.config.get<string>('ADMIN_PASSWORD');
    if (!email || !password) {
      this.log.warn('no users and ADMIN_EMAIL/ADMIN_PASSWORD unset — no admin seeded');
      return;
    }
    await this.users.save(
      this.users.create({ email, passwordHash: await bcrypt.hash(password, 10), role: 'admin' }),
    );
    this.log.log(`seeded initial admin ${email}`);
  }

  async validateUser(email: string, password: string): Promise<User> {
    const user = await this.users.findOne({ where: { email } });
    if (!user || !user.enabled || !(await bcrypt.compare(password, user.passwordHash))) {
      throw new UnauthorizedException('invalid credentials');
    }
    return user;
  }

  async login(user: User): Promise<Tokens> {
    return this.issueTokens(user);
  }

  //! Verify a refresh token AND that it matches the stored (current) hash, then rotate.
  async refresh(refreshToken: string): Promise<Tokens> {
    let payload: JwtPayload;
    try {
      payload = await this.jwt.verifyAsync<JwtPayload>(refreshToken, {
        //! fail-fast: no hardcoded fallback (see secret-config.ts).
        secret: requiredSecret(this.config, 'JWT_REFRESH_SECRET'),
      });
    } catch {
      throw new UnauthorizedException('invalid refresh token');
    }
    const user = await this.users.findOne({ where: { id: payload.sub } });
    if (!user || !user.enabled || !user.refreshTokenHash) {
      throw new UnauthorizedException('refresh not allowed');
    }
    if (!(await bcrypt.compare(tokenDigest(refreshToken), user.refreshTokenHash))) {
      throw new UnauthorizedException('stale refresh token');
    }
    return this.issueTokens(user);
  }

  async logout(userId: string): Promise<void> {
    await this.users.update(userId, { refreshTokenHash: null });
  }

  private async issueTokens(user: User): Promise<Tokens> {
    const payload: JwtPayload = { sub: user.id, email: user.email, role: user.role };
    const accessToken = await this.jwt.signAsync(payload, {
      //! fail-fast: no hardcoded fallback (see secret-config.ts).
      secret: requiredSecret(this.config, 'JWT_SECRET'),
      expiresIn: this.config.get<string>('JWT_ACCESS_TTL', '15m'),
    });
    const refreshToken = await this.jwt.signAsync(payload, {
      secret: requiredSecret(this.config, 'JWT_REFRESH_SECRET'),
      expiresIn: this.config.get<string>('JWT_REFRESH_TTL', '7d'),
    });
    await this.users.update(user.id, {
      refreshTokenHash: await bcrypt.hash(tokenDigest(refreshToken), 10),
    });
    return { accessToken, refreshToken };
  }
}
