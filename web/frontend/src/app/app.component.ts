import { Component, OnInit, inject, signal } from '@angular/core';
import { RouterOutlet } from '@angular/router';
import { MatProgressSpinnerModule } from '@angular/material/progress-spinner';
import { AuthService } from './core/auth.service';

//! Root: attempt a silent session restore (refresh cookie) before painting routes.
@Component({
  selector: 'app-root',
  standalone: true,
  imports: [RouterOutlet, MatProgressSpinnerModule],
  template: `
    @if (booting()) {
      <div class="boot">
        <mat-spinner diameter="36"></mat-spinner>
      </div>
    } @else {
      <router-outlet></router-outlet>
    }
  `,
  styles: [
    `
      .boot {
        display: flex;
        align-items: center;
        justify-content: center;
        height: 100vh;
      }
    `,
  ],
})
export class AppComponent implements OnInit {
  private readonly auth = inject(AuthService);
  readonly booting = signal(true);

  ngOnInit(): void {
    this.auth.bootstrap().subscribe({
      next: () => this.booting.set(false),
      error: () => this.booting.set(false),
    });
  }
}
