import { Component, OnInit, inject, signal } from '@angular/core';
import { RouterLink, RouterLinkActive, RouterOutlet } from '@angular/router';
import { MatToolbarModule } from '@angular/material/toolbar';
import { MatButtonModule } from '@angular/material/button';
import { MatIconModule } from '@angular/material/icon';
import { MatTooltipModule } from '@angular/material/tooltip';
import { ApiService } from '../core/api.service';
import { AuthService } from '../core/auth.service';
import { Health } from '../core/models';

//! Top-level chrome: brand, nav links (Admin hidden for non-admins), health badge, sign-out.
@Component({
  selector: 'app-shell',
  standalone: true,
  imports: [
    RouterOutlet,
    RouterLink,
    RouterLinkActive,
    MatToolbarModule,
    MatButtonModule,
    MatIconModule,
    MatTooltipModule,
  ],
  template: `
    <mat-toolbar class="bar">
      <span class="brand">Thoth</span>
      <nav>
        <a mat-button routerLink="/market-data" routerLinkActive="active">Market Data</a>
        <a mat-button routerLink="/pricing-grid" routerLinkActive="active">Vanilla Grid</a>
        <a mat-button routerLink="/panels" routerLinkActive="active">Panels</a>
        <a mat-button routerLink="/blotter" routerLinkActive="active">Blotter</a>
        @if (auth.isAdmin()) {
          <a mat-button routerLink="/admin" routerLinkActive="active">Admin</a>
        }
      </nav>
      <span class="thoth-spacer"></span>
      @if (health(); as h) {
        <span
          class="health"
          [class.ok]="h.healthy > 0"
          [matTooltip]="h.healthy + '/' + h.engineReplicas + ' engine replicas healthy'"
        >
          <mat-icon>{{ h.healthy > 0 ? 'check_circle' : 'error' }}</mat-icon>
          engine {{ h.healthy }}/{{ h.engineReplicas }}
        </span>
      }
      <span class="user thoth-muted">{{ auth.user()?.email }}</span>
      <button mat-icon-button (click)="auth.logout()" matTooltip="Sign out">
        <mat-icon>logout</mat-icon>
      </button>
    </mat-toolbar>
    <main class="thoth-page">
      <router-outlet></router-outlet>
    </main>
  `,
  styles: [
    `
      .bar {
        background: var(--thoth-bg);
        color: var(--thoth-text);
        border-bottom: 1px solid var(--thoth-border);
        gap: 8px;
      }
      .brand {
        font-weight: 600;
        font-size: 18px;
        margin-right: 16px;
        color: var(--thoth-primary);
      }
      nav a.active {
        font-weight: 600;
        color: var(--thoth-primary);
      }
      .health {
        display: inline-flex;
        align-items: center;
        gap: 4px;
        font-size: 13px;
        color: var(--thoth-negative);
        margin-right: 8px;
      }
      .health.ok {
        color: var(--thoth-text-muted);
      }
      .health mat-icon {
        font-size: 18px;
        width: 18px;
        height: 18px;
      }
      .user {
        font-size: 13px;
        margin: 0 8px;
      }
      main {
        background: var(--thoth-bg);
        min-height: calc(100vh - 64px);
      }
    `,
  ],
})
export class ShellComponent implements OnInit {
  private readonly api = inject(ApiService);
  readonly auth = inject(AuthService);
  readonly health = signal<Health | null>(null);

  ngOnInit(): void {
    this.api.health().subscribe({
      next: (h) => this.health.set(h),
      error: () => this.health.set(null),
    });
  }
}
