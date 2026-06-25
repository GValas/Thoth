//! Tiny fetch wrapper over the BFF (base '/api', proxied to :3000 in dev — see
//! vite.config.ts). The access token lives in memory; the refresh token is an httpOnly
//! cookie the BFF sets, exactly as the Angular app uses it. POC scope: no silent-refresh
//! interceptor — a 401 just bubbles up to the login screen.

import type { GridProgress, GridRun, GridSubmit, Workspace, WsObject } from './types';

const BASE = '/api';

let accessToken: string | null = null;
export const setToken = (t: string | null) => (accessToken = t);
export const getToken = () => accessToken;

async function req<T>(path: string, init: RequestInit = {}): Promise<T> {
  const headers = new Headers(init.headers);
  headers.set('Content-Type', 'application/json');
  if (accessToken) headers.set('Authorization', `Bearer ${accessToken}`);
  const res = await fetch(BASE + path, { ...init, headers, credentials: 'include' });
  if (!res.ok) {
    let msg = `${res.status} ${res.statusText}`;
    try {
      const body = await res.json();
      msg = body.message ?? (Array.isArray(body.errors) ? body.errors.join('; ') : msg);
    } catch {
      /* non-JSON error body */
    }
    throw new Error(msg);
  }
  return res.status === 204 ? (undefined as T) : ((await res.json()) as T);
}

export const api = {
  login: (email: string, password: string) =>
    req<{ accessToken: string }>('/auth/login', {
      method: 'POST',
      body: JSON.stringify({ email, password }),
    }),

  workspaces: () => req<Workspace[]>('/workspaces'),
  createWorkspace: (name: string) =>
    req<Workspace>('/workspaces', { method: 'POST', body: JSON.stringify({ name }) }),

  objects: (workspaceId: string) => req<WsObject[]>(`/workspaces/${workspaceId}/objects`),
  seed: (workspaceId: string, seed: number) =>
    req<WsObject[]>(`/workspaces/${workspaceId}/objects/seed`, {
      method: 'POST',
      body: JSON.stringify({ seed }),
    }),

  submitGrid: (dto: GridSubmit) =>
    req<{ jobId: string }>('/grid', { method: 'POST', body: JSON.stringify(dto) }),
  getGrid: (jobId: string) => req<GridRun>(`/grid/${jobId}`),
  gridProgress: (jobId: string) => req<GridProgress>(`/grid/${jobId}/progress`),
};
