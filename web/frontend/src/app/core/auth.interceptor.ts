import { HttpErrorResponse, HttpInterceptorFn } from '@angular/common/http';
import { inject } from '@angular/core';
import { Router } from '@angular/router';
import { catchError, switchMap, throwError } from 'rxjs';
import { AuthService } from './auth.service';

//! Adds `Authorization: Bearer <token>` and, on a single 401, transparently refreshes
//! the access token (via the httpOnly refresh cookie) and replays the request once.
//! Auth endpoints are exempt to avoid recursion.
export const authInterceptor: HttpInterceptorFn = (req, next) => {
  const auth = inject(AuthService);
  const router = inject(Router);

  const isAuthCall =
    req.url.includes('/api/auth/login') ||
    req.url.includes('/api/auth/refresh') ||
    req.url.includes('/api/health');

  const withAuth = (token: string | null) =>
    token
      ? req.clone({ setHeaders: { Authorization: `Bearer ${token}` } })
      : req;

  return next(withAuth(auth.token)).pipe(
    catchError((err: HttpErrorResponse) => {
      if (err.status !== 401 || isAuthCall) {
        return throwError(() => err);
      }
      // one silent refresh attempt, then replay
      return auth.refresh().pipe(
        switchMap((token) => next(withAuth(token))),
        catchError((refreshErr) => {
          auth.clear();
          router.navigate(['/login']);
          return throwError(() => refreshErr);
        }),
      );
    }),
  );
};
