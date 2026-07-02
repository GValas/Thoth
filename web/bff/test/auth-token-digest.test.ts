import { describe, it, expect } from 'vitest';
import { createHash } from 'node:crypto';
import * as bcrypt from 'bcryptjs';

//! Regression test for the refresh-token rotation check (auth.service.ts).
//!
//! bcrypt reads only the first 72 BYTES of its input. Two refresh JWTs issued to the
//! same user share their first 72 bytes — the constant base64 header plus the leading
//! `{"sub":"<uuid>","email":...` of the payload (iat/exp sit at the END) — so bcrypt-ing
//! the RAW token makes every token of a user compare equal: a rotated-out token would
//! still pass the "stale refresh token" check. auth.service.ts therefore sha256-digests
//! the token (64 hex chars, fully significant) before bcrypt. This test pins both the
//! failure mode of the raw scheme and the correctness of the digested one.

const digest = (t: string) => createHash('sha256').update(t).digest('hex');

//! two realistic same-user JWTs: identical header + payload prefix, differing only in
//! the (late) iat/exp claims and the signature.
function sameUserTokens(): [string, string] {
  const header = Buffer.from(JSON.stringify({ alg: 'HS256', typ: 'JWT' })).toString('base64url');
  const payload = (iat: number) =>
    Buffer.from(
      JSON.stringify({
        sub: '3f8a1c2e-9b47-4d15-8a6f-0c2d4e6f8a1b',
        email: 'trader@example.com',
        role: 'user',
        iat,
        exp: iat + 7 * 24 * 3600,
      }),
    ).toString('base64url');
  return [
    `${header}.${payload(1_750_000_000)}.sig-aaaaaaaaaaaaaaaaaaaaaa`,
    `${header}.${payload(1_750_000_060)}.sig-bbbbbbbbbbbbbbbbbbbbbb`,
  ];
}

describe('refresh-token digest scheme', () => {
  it('raw bcrypt WOULD accept a rotated-out same-user token (the bug being fixed)', async () => {
    const [oldToken, newToken] = sameUserTokens();
    expect(oldToken).not.toEqual(newToken);
    expect(oldToken.slice(0, 72)).toEqual(newToken.slice(0, 72)); // shared 72-byte prefix
    const rawHashOfNew = await bcrypt.hash(newToken, 4);
    expect(await bcrypt.compare(oldToken, rawHashOfNew)).toBe(true); // truncation collision
  });

  it('sha256-digested bcrypt rejects a rotated-out token and accepts the current one', async () => {
    const [oldToken, newToken] = sameUserTokens();
    const storedHash = await bcrypt.hash(digest(newToken), 4);
    expect(await bcrypt.compare(digest(newToken), storedHash)).toBe(true); // current ok
    expect(await bcrypt.compare(digest(oldToken), storedHash)).toBe(false); // stale rejected
  });
});
