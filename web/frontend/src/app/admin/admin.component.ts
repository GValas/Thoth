import { Component, inject, signal } from '@angular/core';
import { FormsModule } from '@angular/forms';
import { MatCardModule } from '@angular/material/card';
import { MatTableModule } from '@angular/material/table';
import { MatButtonModule } from '@angular/material/button';
import { MatIconModule } from '@angular/material/icon';
import { MatSlideToggleModule } from '@angular/material/slide-toggle';
import { MatSelectModule } from '@angular/material/select';
import { MatFormFieldModule } from '@angular/material/form-field';
import { MatInputModule } from '@angular/material/input';
import { MatSnackBar, MatSnackBarModule } from '@angular/material/snack-bar';
import { ApiService } from '../core/api.service';
import { UserRole, UserRow } from '../core/models';

//! Admin user management table: create / enable-disable / role / password reset / delete.
//! Route is RoleGuard-protected to 'admin'; the nav link is hidden otherwise.
@Component({
  selector: 'app-admin',
  standalone: true,
  imports: [
    FormsModule,
    MatCardModule,
    MatTableModule,
    MatButtonModule,
    MatIconModule,
    MatSlideToggleModule,
    MatSelectModule,
    MatFormFieldModule,
    MatInputModule,
    MatSnackBarModule,
  ],
  templateUrl: './admin.component.html',
  styleUrl: './admin.component.scss',
})
export class AdminComponent {
  private readonly api = inject(ApiService);
  private readonly snack = inject(MatSnackBar);

  readonly users = signal<UserRow[]>([]);
  readonly cols = ['email', 'role', 'enabled', 'actions'];

  // new-user form
  newEmail = '';
  newPassword = '';
  newRole: UserRole = 'user';

  constructor() {
    this.load();
  }

  load(): void {
    this.api.listUsers().subscribe({
      next: (u) => this.users.set(u),
      error: (e) => this.fail(e),
    });
  }

  create(): void {
    if (!this.newEmail || this.newPassword.length < 8) {
      this.snack.open('Email and an 8+ char password are required', 'OK', { duration: 3000 });
      return;
    }
    this.api.createUser(this.newEmail, this.newPassword, this.newRole).subscribe({
      next: () => {
        this.newEmail = '';
        this.newPassword = '';
        this.newRole = 'user';
        this.snack.open('User created', 'OK', { duration: 2500 });
        this.load();
      },
      error: (e) => this.fail(e),
    });
  }

  toggleEnabled(u: UserRow): void {
    this.api.setUserEnabled(u.id, !u.enabled).subscribe({
      next: () => this.load(),
      error: (e) => this.fail(e),
    });
  }

  setRole(u: UserRow, role: UserRole): void {
    this.api.setUserRole(u.id, role).subscribe({
      next: () => this.load(),
      error: (e) => this.fail(e),
    });
  }

  resetPassword(u: UserRow): void {
    const pw = prompt(`New password for ${u.email} (8+ chars):`);
    if (!pw) return;
    if (pw.length < 8) {
      this.snack.open('Password must be at least 8 characters', 'OK', { duration: 3000 });
      return;
    }
    this.api.setUserPassword(u.id, pw).subscribe({
      next: () => this.snack.open('Password reset', 'OK', { duration: 2500 }),
      error: (e) => this.fail(e),
    });
  }

  remove(u: UserRow): void {
    if (!confirm(`Delete ${u.email}?`)) return;
    this.api.deleteUser(u.id).subscribe({
      next: () => this.load(),
      error: (e) => this.fail(e),
    });
  }

  private fail(e: unknown): void {
    const err = e as { error?: { message?: string } };
    this.snack.open(err.error?.message ?? 'Request failed', 'OK', { duration: 3500 });
  }
}
