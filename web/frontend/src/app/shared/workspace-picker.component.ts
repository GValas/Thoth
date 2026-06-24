import { Component, EventEmitter, OnInit, Output, inject, signal } from '@angular/core';
import { FormsModule } from '@angular/forms';
import { MatFormFieldModule } from '@angular/material/form-field';
import { MatSelectModule } from '@angular/material/select';
import { MatButtonModule } from '@angular/material/button';
import { MatIconModule } from '@angular/material/icon';
import { MatDialog, MatDialogModule } from '@angular/material/dialog';
import { MatInputModule } from '@angular/material/input';
import { ApiService } from '../core/api.service';
import { Workspace } from '../core/models';
import { NewWorkspaceDialog } from './new-workspace.dialog';

//! Reusable workspace selector with a "+ New" affordance. Emits the chosen Workspace.
@Component({
  selector: 'app-workspace-picker',
  standalone: true,
  imports: [
    FormsModule,
    MatFormFieldModule,
    MatSelectModule,
    MatButtonModule,
    MatIconModule,
    MatDialogModule,
    MatInputModule,
  ],
  template: `
    <div class="picker">
      <mat-form-field appearance="outline" subscriptSizing="dynamic">
        <mat-label>Workspace</mat-label>
        <mat-select [(ngModel)]="selectedId" (selectionChange)="emit()">
          @for (ws of workspaces(); track ws.id) {
            <mat-option [value]="ws.id">{{ ws.name }} ({{ ws.currency }}, {{ ws.today }})</mat-option>
          }
        </mat-select>
      </mat-form-field>
      <button mat-stroked-button (click)="create()">
        <mat-icon>add</mat-icon> New
      </button>
    </div>
  `,
  styles: [
    `
      .picker {
        display: flex;
        gap: 8px;
        align-items: center;
      }
      mat-form-field {
        min-width: 280px;
      }
    `,
  ],
})
export class WorkspacePickerComponent implements OnInit {
  private readonly api = inject(ApiService);
  private readonly dialog = inject(MatDialog);

  @Output() selected = new EventEmitter<Workspace>();

  readonly workspaces = signal<Workspace[]>([]);
  selectedId = '';

  ngOnInit(): void {
    this.load();
  }

  private load(selectId?: string): void {
    this.api.listWorkspaces().subscribe((ws) => {
      this.workspaces.set(ws);
      if (selectId) {
        this.selectedId = selectId;
        this.emit();
      }
    });
  }

  emit(): void {
    const ws = this.workspaces().find((w) => w.id === this.selectedId);
    if (ws) this.selected.emit(ws);
  }

  create(): void {
    this.dialog
      .open(NewWorkspaceDialog, { width: '360px' })
      .afterClosed()
      .subscribe((dto) => {
        if (!dto) return;
        this.api.createWorkspace(dto).subscribe((ws) => this.load(ws.id));
      });
  }
}
