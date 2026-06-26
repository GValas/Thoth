import { HttpClient } from '@angular/common/http';
import { Injectable, inject } from '@angular/core';
import { Observable } from 'rxjs';
import {
  AuthUser,
  CreateWorkspace,
  GridMatrix,
  GridMeta,
  GridProgress,
  GridResult,
  GridSubmit,
  Health,
  InstrumentPriceRequest,
  InstrumentPriceResponse,
  SchemaResponse,
  SeedRequest,
  TokenResponse,
  UpdateWorkspace,
  UserRole,
  UserRow,
  Workspace,
  WsObject,
} from './models';

//! Typed thin wrapper over the BFF. baseUrl is '/api' (proxied to :3000 in dev).
@Injectable({ providedIn: 'root' })
export class ApiService {
  private readonly http = inject(HttpClient);
  private readonly base = '/api';

  // --- auth ---
  login(email: string, password: string): Observable<TokenResponse> {
    return this.http.post<TokenResponse>(`${this.base}/auth/login`, { email, password });
  }
  refresh(): Observable<TokenResponse> {
    return this.http.post<TokenResponse>(`${this.base}/auth/refresh`, {});
  }
  logout(): Observable<{ ok: boolean }> {
    return this.http.post<{ ok: boolean }>(`${this.base}/auth/logout`, {});
  }
  me(): Observable<AuthUser> {
    return this.http.get<AuthUser>(`${this.base}/auth/me`);
  }

  // --- health / schema ---
  health(): Observable<Health> {
    return this.http.get<Health>(`${this.base}/health`);
  }
  schema(): Observable<SchemaResponse> {
    return this.http.get<SchemaResponse>(`${this.base}/schema`);
  }

  // --- workspaces ---
  listWorkspaces(): Observable<Workspace[]> {
    return this.http.get<Workspace[]>(`${this.base}/workspaces`);
  }
  getWorkspace(id: string): Observable<Workspace> {
    return this.http.get<Workspace>(`${this.base}/workspaces/${id}`);
  }
  createWorkspace(dto: CreateWorkspace): Observable<Workspace> {
    return this.http.post<Workspace>(`${this.base}/workspaces`, dto);
  }
  updateWorkspace(id: string, dto: UpdateWorkspace): Observable<Workspace> {
    return this.http.put<Workspace>(`${this.base}/workspaces/${id}`, dto);
  }

  // --- objects (market data) ---
  listObjects(workspaceId: string): Observable<WsObject[]> {
    return this.http.get<WsObject[]>(`${this.base}/workspaces/${workspaceId}/objects`);
  }
  replaceObjects(workspaceId: string, objects: WsObject[]): Observable<unknown> {
    return this.http.put(`${this.base}/workspaces/${workspaceId}/objects`, { objects });
  }
  //! generate a random sample market-data set; replaces the workspace's objects.
  seedObjects(workspaceId: string, req: SeedRequest = {}): Observable<WsObject[]> {
    return this.http.post<WsObject[]>(`${this.base}/workspaces/${workspaceId}/objects/seed`, req);
  }

  // --- grid ---
  submitGrid(dto: GridSubmit): Observable<{ jobId: string }> {
    return this.http.post<{ jobId: string }>(`${this.base}/grid`, dto);
  }
  //! synchronous live re-price (live spots overlaid) — returns matrices directly, no job.
  priceGridLive(dto: GridSubmit): Observable<{ matrices: GridMatrix[]; meta: GridMeta }> {
    return this.http.post<{ matrices: GridMatrix[]; meta: GridMeta }>(`${this.base}/grid/live`, dto);
  }
  getGrid(id: string): Observable<GridResult> {
    return this.http.get<GridResult>(`${this.base}/grid/${id}`);
  }
  getGridProgress(id: string): Observable<GridProgress> {
    return this.http.get<GridProgress>(`${this.base}/grid/${id}/progress`);
  }

  // --- single-instrument pricing (panels + blotter) ---
  //! synchronous price of one hand-entered instrument (premium + Greeks). `live: true`
  //! overlays the latest live spots, so panels/blotter can quote off the live feed.
  priceInstrument(dto: InstrumentPriceRequest): Observable<InstrumentPriceResponse> {
    return this.http.post<InstrumentPriceResponse>(`${this.base}/instrument/price`, dto);
  }

  // --- admin ---
  listUsers(): Observable<UserRow[]> {
    return this.http.get<UserRow[]>(`${this.base}/admin/users`);
  }
  createUser(email: string, password: string, role: UserRole): Observable<UserRow> {
    return this.http.post<UserRow>(`${this.base}/admin/users`, { email, password, role });
  }
  setUserEnabled(id: string, enabled: boolean): Observable<UserRow> {
    return this.http.patch<UserRow>(`${this.base}/admin/users/${id}/enabled`, { enabled });
  }
  setUserRole(id: string, role: UserRole): Observable<UserRow> {
    return this.http.patch<UserRow>(`${this.base}/admin/users/${id}/role`, { role });
  }
  setUserPassword(id: string, password: string): Observable<UserRow> {
    return this.http.patch<UserRow>(`${this.base}/admin/users/${id}/password`, { password });
  }
  deleteUser(id: string): Observable<unknown> {
    return this.http.delete(`${this.base}/admin/users/${id}`);
  }
}
