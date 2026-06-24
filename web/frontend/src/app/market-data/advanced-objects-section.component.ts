import { Component, Input, computed, inject, signal } from '@angular/core';
import { FormsModule, ReactiveFormsModule, FormGroup } from '@angular/forms';
import { MatListModule } from '@angular/material/list';
import { MatButtonModule } from '@angular/material/button';
import { MatIconModule } from '@angular/material/icon';
import { MatFormFieldModule } from '@angular/material/form-field';
import { MatSelectModule } from '@angular/material/select';
import { FormlyModule, FormlyFieldConfig } from '@ngx-formly/core';
import { FormlyMaterialModule } from '@ngx-formly/material';
import { FormlyJsonschema } from '@ngx-formly/core/json-schema';
import { JSONSchemaDef, SchemaResponse, WsObject } from '../core/models';
import { DASHBOARD_KINDS, MarketModel } from './market-model';

//! Escape hatch for objects the four dashboard areas don't render natively (calendars,
//! baskets, rainbows, composites, fixings, …). Reuses the schema-driven ngx-formly editor
//! so nothing is lost on save. Lets the user add an object of any kind too.
@Component({
  selector: 'app-advanced-objects-section',
  standalone: true,
  imports: [
    FormsModule,
    ReactiveFormsModule,
    MatListModule,
    MatButtonModule,
    MatIconModule,
    MatFormFieldModule,
    MatSelectModule,
    FormlyModule,
    FormlyMaterialModule,
  ],
  template: `
    <div class="adv">
      <div class="list">
        <mat-form-field appearance="outline" subscriptSizing="dynamic">
          <mat-label>Add object of kind…</mat-label>
          <mat-select [ngModel]="null" (ngModelChange)="add($event)">
            @for (k of kinds(); track k) {
              <mat-option [value]="k">{{ k }}</mat-option>
            }
          </mat-select>
        </mat-form-field>
        <mat-nav-list dense>
          @for (o of objects(); track o.name) {
            <a mat-list-item [class.active]="o.name === selectedName()" (click)="select(o)">
              {{ o.name }} <span class="kind">{{ o.kind }}</span>
            </a>
          }
          @if (objects().length === 0) {
            <p class="empty">No other objects.</p>
          }
        </mat-nav-list>
      </div>
      <div class="editor">
        @if (selectedName()) {
          <form [formGroup]="form">
            <formly-form [form]="form" [fields]="fields" [model]="formModel"></formly-form>
          </form>
          <button mat-stroked-button color="warn" (click)="removeSelected()">
            <mat-icon>delete</mat-icon> Delete
          </button>
        } @else {
          <p class="empty">Select an object to edit.</p>
        }
      </div>
    </div>
  `,
  styles: [
    `
      .adv {
        display: grid;
        grid-template-columns: 260px 1fr;
        gap: 16px;
      }
      .kind {
        color: var(--thoth-text-muted, #888);
        font-size: 11px;
        margin-left: 6px;
      }
      .active {
        background: var(--thoth-surface, #eef);
      }
      .empty {
        color: var(--thoth-text-muted, #888);
      }
    `,
  ],
})
export class AdvancedObjectsSectionComponent {
  @Input({ required: true }) model!: MarketModel;
  @Input() schema: SchemaResponse | null = null;

  private readonly formlyJson = inject(FormlyJsonschema);

  readonly selectedName = signal<string | null>(null);
  readonly objects = computed(() => this.model.advanced());
  readonly kinds = computed(() => (this.schema?.kinds ?? []).filter((k) => !DASHBOARD_KINDS.has(k)));

  form = new FormGroup({});
  fields: FormlyFieldConfig[] = [];
  formModel: Record<string, unknown> = {};

  select(obj: WsObject): void {
    this.selectedName.set(obj.name);
    this.buildForm(obj.kind, structuredClone(obj.payload));
  }

  add(kind: string | null): void {
    if (!kind) return;
    const name = this.model.freshName(kind.slice(0, 3) + '_');
    const obj: WsObject = { name, kind, payload: {} };
    this.model.upsert(obj);
    this.select(obj);
  }

  removeSelected(): void {
    const n = this.selectedName();
    if (n) this.model.remove(n);
    this.selectedName.set(null);
    this.fields = [];
  }

  //! Build a formly config from the kind's $def, re-attaching $defs so internal $ref
  //! pointers resolve, and fold edits back into the store on every change.
  private buildForm(kind: string, payload: Record<string, unknown>): void {
    const s = this.schema;
    const def = s?.defs[kind] as JSONSchemaDef | undefined;
    if (!s || !def) {
      this.fields = [];
      return;
    }
    this.form = new FormGroup({});
    this.formModel = payload;
    this.fields = [this.formlyJson.toFieldConfig({ ...def, $defs: s.defs } as never)];
    this.form.valueChanges.subscribe(() => {
      const name = this.selectedName();
      if (name) this.model.replace(name, { ...this.formModel });
    });
  }
}
