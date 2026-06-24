import { Component, inject } from '@angular/core';
import { FormBuilder, ReactiveFormsModule, Validators } from '@angular/forms';
import { MatDialogModule, MatDialogRef } from '@angular/material/dialog';
import { MatFormFieldModule } from '@angular/material/form-field';
import { MatInputModule } from '@angular/material/input';
import { MatButtonModule } from '@angular/material/button';
import { CreateWorkspace } from '../core/models';

@Component({
  selector: 'app-new-workspace-dialog',
  standalone: true,
  imports: [
    ReactiveFormsModule,
    MatDialogModule,
    MatFormFieldModule,
    MatInputModule,
    MatButtonModule,
  ],
  template: `
    <h2 mat-dialog-title>New workspace</h2>
    <mat-dialog-content>
      <form [formGroup]="form">
        <mat-form-field appearance="outline">
          <mat-label>Name</mat-label>
          <input matInput formControlName="name" />
        </mat-form-field>
        <mat-form-field appearance="outline">
          <mat-label>Valuation date (today)</mat-label>
          <input matInput formControlName="today" placeholder="YYYY-MM-DD" />
        </mat-form-field>
        <mat-form-field appearance="outline">
          <mat-label>Currency</mat-label>
          <input matInput formControlName="currency" placeholder="eur" />
        </mat-form-field>
      </form>
    </mat-dialog-content>
    <mat-dialog-actions align="end">
      <button mat-button mat-dialog-close>Cancel</button>
      <button mat-flat-button color="primary" [disabled]="form.invalid" (click)="save()">
        Create
      </button>
    </mat-dialog-actions>
  `,
  styles: [
    `
      form {
        display: flex;
        flex-direction: column;
        gap: 4px;
        padding-top: 8px;
      }
      mat-form-field {
        width: 100%;
      }
    `,
  ],
})
export class NewWorkspaceDialog {
  private readonly fb = inject(FormBuilder);
  private readonly ref = inject(MatDialogRef<NewWorkspaceDialog>);

  readonly form = this.fb.nonNullable.group({
    name: ['', Validators.required],
    today: [''],
    currency: [''],
  });

  save(): void {
    const v = this.form.getRawValue();
    const dto: CreateWorkspace = { name: v.name };
    if (v.today) dto.today = v.today;
    if (v.currency) dto.currency = v.currency;
    this.ref.close(dto);
  }
}
