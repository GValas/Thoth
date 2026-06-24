import { Injectable, computed, inject, signal } from '@angular/core';
import { Router } from '@angular/router';
import { Observable, map, tap } from 'rxjs';
import { ApiService } from './api.service';
import { AuthUser } from './models';

//! Holds the access token in memory only (refresh lives in an httpOnly cookie).
//! Silent refresh on 401 is driven by the HTTP interceptor; this service owns the token.
@Injectable({ providedIn: 'root' })
export class AuthService {
  private readonly api = inject(ApiService);
  private readonly router = inject(Router);

  private readonly _token = signal<string | null>(null);
  private readonly _user = signal<AuthUser | null>(null);

  readonly user = this._user.asReadonly();
  readonly isAuthenticated = computed(() => this._token() !== null);
  readonly isAdmin = computed(() => this._user()?.role === 'admin');

  get token(): string | null {
    return this._token();
  }

  login(email: string, password: string): Observable<AuthUser> {
    return new Observable<AuthUser>((sub) => {
      this.api.login(email, password).subscribe({
        next: (res) => {
          this._token.set(res.accessToken);
          this.api.me().subscribe({
            next: (u) => {
              this._user.set(u);
              sub.next(u);
              sub.complete();
            },
            error: (e) => sub.error(e),
          });
        },
        error: (e) => sub.error(e),
      });
    });
  }

  //! Used by the interceptor to recover from a 401. Returns the fresh access token.
  refresh(): Observable<string> {
    return this.api.refresh().pipe(
      tap((res) => this._token.set(res.accessToken)),
      map((res) => res.accessToken),
    );
  }

  //! Best-effort: try to restore a session on app start via the refresh cookie.
  bootstrap(): Observable<AuthUser> {
    return new Observable<AuthUser>((sub) => {
      this.api.refresh().subscribe({
        next: (res) => {
          this._token.set(res.accessToken);
          this.api.me().subscribe({
            next: (u) => {
              this._user.set(u);
              sub.next(u);
              sub.complete();
            },
            error: (e) => sub.error(e),
          });
        },
        error: (e) => sub.error(e),
      });
    });
  }

  logout(): void {
    const done = () => {
      this.clear();
      void this.router.navigate(['/login']); // navigate so the auth guard fires at once
    };
    this.api.logout().subscribe({ next: done, error: done });
  }

  clear(): void {
    this._token.set(null);
    this._user.set(null);
  }

  setToken(token: string | null): void {
    this._token.set(token);
  }
}
