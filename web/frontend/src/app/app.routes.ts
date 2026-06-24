import { Routes } from '@angular/router';
import { authGuard, roleGuard } from './core/guards';

export const routes: Routes = [
  {
    path: 'login',
    loadComponent: () => import('./auth/login.component').then((m) => m.LoginComponent),
  },
  {
    path: '',
    canActivate: [authGuard],
    loadComponent: () => import('./shell/shell.component').then((m) => m.ShellComponent),
    children: [
      { path: '', pathMatch: 'full', redirectTo: 'market-data' },
      {
        path: 'market-data',
        loadComponent: () =>
          import('./market-data/market-data.component').then((m) => m.MarketDataComponent),
      },
      {
        path: 'pricing-grid',
        loadComponent: () =>
          import('./pricing-grid/pricing-grid.component').then((m) => m.PricingGridComponent),
      },
      {
        path: 'admin',
        canActivate: [roleGuard],
        loadComponent: () => import('./admin/admin.component').then((m) => m.AdminComponent),
      },
    ],
  },
  { path: '**', redirectTo: '' },
];
