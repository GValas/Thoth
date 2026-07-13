import { Component, EventEmitter, Input, Output } from '@angular/core';
import { FormsModule } from '@angular/forms';
import { MatIconModule } from '@angular/material/icon';

export type SortDir = 'asc' | 'desc' | null;

//! A blotter column header: a click-to-sort title (with an asc/desc/none arrow) plus an
//! optional per-column text filter box. Presentational — the parent owns the sort/filter
//! state and reacts to the (sort) / (filterChange) outputs.
@Component({
  selector: 'app-blotter-col-header',
  standalone: true,
  imports: [FormsModule, MatIconModule],
  template: `
    <button type="button" class="bch-title" (click)="sort.emit(col)" [attr.aria-label]="'Sort by ' + label">
      <span>{{ label }}</span>
      <mat-icon class="bch-arrow" [class.on]="sortDir">{{
        sortDir === 'asc' ? 'arrow_upward' : sortDir === 'desc' ? 'arrow_downward' : 'unfold_more'
      }}</mat-icon>
    </button>
    @if (filterable) {
      <input
        class="bch-filter"
        type="text"
        [ngModel]="filter"
        (ngModelChange)="filterChange.emit($event)"
        (click)="$event.stopPropagation()"
        (keydown)="$event.stopPropagation()"
        placeholder="filter"
        aria-label="Column filter"
      />
    }
  `,
  styles: [
    `
      :host {
        display: flex;
        flex-direction: column;
        gap: 3px;
        align-items: stretch;
      }
      .bch-title {
        display: inline-flex;
        align-items: center;
        gap: 2px;
        cursor: pointer;
        background: none;
        border: none;
        padding: 0;
        font: inherit;
        font-weight: 600;
        color: inherit;
        text-align: left;
        white-space: nowrap;
      }
      .bch-arrow {
        font-size: 15px;
        width: 15px;
        height: 15px;
        opacity: 0.25;
      }
      .bch-arrow.on {
        opacity: 0.9;
      }
      .bch-filter {
        width: 100%;
        min-width: 46px;
        box-sizing: border-box;
        font: inherit;
        font-size: 11px;
        font-weight: 400;
        padding: 1px 4px;
        border: 1px solid var(--thoth-border);
        border-radius: 3px;
        background: var(--thoth-surface);
        color: var(--thoth-text);
      }
    `,
  ],
})
export class BlotterColHeaderComponent {
  @Input() label = '';
  @Input() col = '';
  @Input() sortDir: SortDir = null;
  @Input() filter = '';
  @Input() filterable = true;
  @Output() sort = new EventEmitter<string>();
  @Output() filterChange = new EventEmitter<string>();
}
