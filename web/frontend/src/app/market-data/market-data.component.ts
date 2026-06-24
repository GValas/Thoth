import { Component, computed, inject, signal } from '@angular/core';
import { FormsModule, ReactiveFormsModule, FormGroup } from '@angular/forms';
import { MatCardModule } from '@angular/material/card';
import { MatListModule } from '@angular/material/list';
import { MatButtonModule } from '@angular/material/button';
import { MatIconModule } from '@angular/material/icon';
import { MatFormFieldModule } from '@angular/material/form-field';
import { MatSelectModule } from '@angular/material/select';
import { MatInputModule } from '@angular/material/input';
import { MatSnackBar, MatSnackBarModule } from '@angular/material/snack-bar';
import { MatExpansionModule } from '@angular/material/expansion';
import { FormlyModule, FormlyFieldConfig } from '@ngx-formly/core';
import { FormlyMaterialModule } from '@ngx-formly/material';
import { FormlyJsonschema } from '@ngx-formly/core/json-schema';
import { ApiService } from '../core/api.service';
import { JSONSchemaDef, SchemaResponse, Workspace, WsObject } from '../core/models';
import { WorkspacePickerComponent } from '../shared/workspace-picker.component';

//! Market-data editor: pick workspace -> list objects by kind -> schema-driven formly
//! form (branch by the object's kind, defs from GET /api/schema). PERCENT & length
//! reminders are surfaced; references are by object name. Save -> PUT objects.
@Component({
  selector: 'app-market-data',
  standalone: true,
  imports: [
    FormsModule,
    ReactiveFormsModule,
    MatCardModule,
    MatListModule,
    MatButtonModule,
    MatIconModule,
    MatFormFieldModule,
    MatSelectModule,
    MatInputModule,
    MatSnackBarModule,
    MatExpansionModule,
    FormlyModule,
    FormlyMaterialModule,
    WorkspacePickerComponent,
  ],
  templateUrl: './market-data.component.html',
  styleUrl: './market-data.component.scss',
})
export class MarketDataComponent {
  private readonly api = inject(ApiService);
  private readonly formlyJson = inject(FormlyJsonschema);
  private readonly snack = inject(MatSnackBar);

  readonly workspace = signal<Workspace | null>(null);
  readonly schema = signal<SchemaResponse | null>(null);
  readonly objects = signal<WsObject[]>([]);
  readonly selectedName = signal<string | null>(null);
  readonly errors = signal<Record<string, string[]>>({});

  // formly state for the currently-edited object
  form = new FormGroup({});
  fields: FormlyFieldConfig[] = [];
  model: Record<string, unknown> = {};

  readonly kinds = computed(() => this.schema()?.kinds ?? []);
  readonly selected = computed(() =>
    this.objects().find((o) => o.name === this.selectedName()) ?? null,
  );

  // objects grouped by kind for the sidebar
  readonly grouped = computed(() => {
    const groups = new Map<string, WsObject[]>();
    for (const o of this.objects()) {
      const list = groups.get(o.kind) ?? [];
      list.push(o);
      groups.set(o.kind, list);
    }
    return [...groups.entries()].sort((a, b) => a[0].localeCompare(b[0]));
  });

  constructor() {
    this.api.schema().subscribe((s) => this.schema.set(s));
  }

  onWorkspace(ws: Workspace): void {
    this.workspace.set(ws);
    this.selectedName.set(null);
    this.fields = [];
    this.api.listObjects(ws.id).subscribe((objs) => this.objects.set(objs));
  }

  select(obj: WsObject): void {
    this.selectedName.set(obj.name);
    this.buildForm(obj.kind, structuredClone(obj.payload));
  }

  addObject(kind: string): void {
    const base = kind.slice(0, 3);
    let i = this.objects().length + 1;
    let name = `${base}_${i}`;
    const existing = new Set(this.objects().map((o) => o.name));
    while (existing.has(name)) name = `${base}_${++i}`;
    const obj: WsObject = { name, kind, payload: {} };
    this.objects.update((list) => [...list, obj]);
    this.select(obj);
  }

  //! Build a formly config from the kind's $def. Internal `$ref` pointers
  //! (#/$defs/ref, #/$defs/date, ...) are resolved by re-attaching the whole defs map
  //! as `$defs` on the schema we hand to FormlyJsonschema. We branch by kind — no anyOf.
  private buildForm(kind: string, payload: Record<string, unknown>): void {
    const s = this.schema();
    if (!s) return;
    const def = s.defs[kind] as JSONSchemaDef | undefined;
    if (!def) {
      this.fields = [];
      return;
    }
    const schemaForKind = {
      ...def,
      $defs: this.rawDefs(s),
    } as Record<string, unknown>;

    this.form = new FormGroup({});
    this.model = payload;
    this.fields = [this.formlyJson.toFieldConfig(schemaForKind as never)];
  }

  //! The BFF exposes defs keyed by kind, but the internal $ref pointers reference the
  //! raw $def keys (ref, date, refList, ...). Those primitive helpers happen to be
  //! exposed too (ref/refList/date/...); keep the kind-keyed map and add the helpers
  //! under their bare keys so #/$defs/<key> resolves.
  private rawDefs(s: SchemaResponse): Record<string, unknown> {
    // The kind-keyed defs map already contains the primitive helpers (ref, date, ...)
    // because the BFF only surfaces titled kinds; primitives are referenced by key.
    // We therefore include both: kind-name keys AND any nested helper defs we can find.
    const out: Record<string, unknown> = {};
    for (const [k, v] of Object.entries(s.defs)) out[k] = v;
    return out;
  }

  validate(): void {
    const ws = this.workspace();
    if (!ws) return;
    this.api.validateObjects(ws.id, this.currentObjects()).subscribe({
      next: (res) => {
        this.errors.set(res.errors);
        const n = Object.keys(res.errors).length;
        this.snack.open(n === 0 ? 'All objects valid' : `${n} object(s) have errors`, 'OK', {
          duration: 3000,
        });
      },
      error: (e) => this.handleError(e),
    });
  }

  save(): void {
    const ws = this.workspace();
    if (!ws) return;
    this.commitCurrent();
    this.api.replaceObjects(ws.id, this.currentObjects()).subscribe({
      next: () => {
        this.errors.set({});
        this.snack.open('Saved', 'OK', { duration: 2500 });
      },
      error: (e) => this.handleError(e),
    });
  }

  //! Fold the in-progress formly model back into the selected object before persisting.
  private commitCurrent(): void {
    const name = this.selectedName();
    if (!name) return;
    this.objects.update((list) =>
      list.map((o) => (o.name === name ? { ...o, payload: { ...this.model } } : o)),
    );
  }

  private currentObjects(): WsObject[] {
    this.commitCurrent();
    return this.objects();
  }

  private handleError(e: unknown): void {
    const err = e as { status?: number; error?: { errors?: Record<string, string[]>; message?: string } };
    if (err.status === 400 && err.error?.errors) {
      this.errors.set(err.error.errors);
      this.snack.open('Validation failed — see errors', 'OK', { duration: 3500 });
    } else {
      this.snack.open(err.error?.message ?? 'Request failed', 'OK', { duration: 3500 });
    }
  }
}
