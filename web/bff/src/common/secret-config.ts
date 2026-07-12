//! Fail-fast secret handling. The BFF signs/verifies JWTs with JWT_SECRET / JWT_REFRESH_SECRET.
//! A hardcoded fallback (the old `'dev-access-secret-change-me'`) would let the app boot with a
//! guessable signing key in production, so we refuse to run unless a real secret is configured.

import type { ConfigService } from '@nestjs/config';

//! A secret is unacceptable if it is missing, too short to resist brute force, or an obvious
//! placeholder. `/change-me|dev-(access|refresh)|^\d+$/i` catches the shipped defaults, any
//! "…change-me…" value, and trivially numeric secrets; we also reject the two exact legacy
//! defaults explicitly so they can never slip through.
const MIN_SECRET_LENGTH = 32;
const PLACEHOLDER_RE = /change-me|dev-(access|refresh)|^\d+$/i;
const LEGACY_DEFAULTS = new Set(['dev-access-secret-change-me', 'dev-refresh-secret-change-me']);

//! Return true when `value` is safe to use as a JWT signing secret.
function isAcceptableSecret(value: string | undefined): value is string {
  if (!value || value.length < MIN_SECRET_LENGTH) return false;
  if (LEGACY_DEFAULTS.has(value)) return false;
  return !PLACEHOLDER_RE.test(value);
}

//! Read a required secret from config, throwing (fatally) if it is missing/weak/placeholder.
//! Used both at bootstrap and where a value is needed at construction time (jwt.strategy),
//! since a Passport strategy needs its secret synchronously in its constructor.
export function requiredSecret(config: ConfigService, key: string): string {
  const value = config.get<string>(key);
  if (!isAcceptableSecret(value)) {
    throw new Error(
      `${key} is missing, shorter than ${MIN_SECRET_LENGTH} chars, or a known placeholder — ` +
        'set a strong, unique secret before starting the BFF.',
    );
  }
  return value;
}

//! Startup gate: verify BOTH JWT secrets up front so the app refuses to boot with a weak or
//! default signing key rather than failing later on the first login. Called from bootstrap.
export function validateSecurityConfig(config: ConfigService): void {
  requiredSecret(config, 'JWT_SECRET');
  requiredSecret(config, 'JWT_REFRESH_SECRET');
}
