import { Injectable } from '@nestjs/common';
import { ConfigService } from '@nestjs/config';
import { PassportStrategy } from '@nestjs/passport';
import { ExtractJwt, Strategy } from 'passport-jwt';
import type { AuthUser } from '../common/decorators';
import { requiredSecret } from '../common/secret-config';

export interface JwtPayload {
  sub: string;
  email: string;
  role: 'admin' | 'user';
}

//! Validates the Bearer access token and exposes the principal as req.user.
@Injectable()
export class JwtStrategy extends PassportStrategy(Strategy) {
  constructor(config: ConfigService) {
    super({
      jwtFromRequest: ExtractJwt.fromAuthHeaderAsBearerToken(),
      ignoreExpiration: false,
      //! fail-fast: no hardcoded fallback — a weak/missing secret aborts construction (boot).
      secretOrKey: requiredSecret(config, 'JWT_SECRET'),
    });
  }

  validate(payload: JwtPayload): AuthUser {
    return { userId: payload.sub, email: payload.email, role: payload.role };
  }
}
