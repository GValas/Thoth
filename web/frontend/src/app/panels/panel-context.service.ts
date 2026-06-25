import { Injectable, computed, inject, signal } from '@angular/core';
import { ApiService } from '../core/api.service';
import { Workspace, WsObject } from '../core/models';

//! Object kinds that can stand as an instrument's underlying (mirrors the pricing grid).
const UNDERLYING_KINDS = ['equity', 'basket', 'forex', 'rainbow', 'composite'];

//! Root-scoped shared context for the pricing panels: resolves the default workspace and
//! its saved market-data objects ONCE (then just refreshes objects on later panel entries),
//! exposing the underlyings and currencies every panel picks from. Kept out of the panel
//! components so the three panels share one workspace/object load and survive tab nav.
@Injectable({ providedIn: 'root' })
export class PanelContextService {
  private readonly api = inject(ApiService);
  private loaded = false;

  readonly workspace = signal<Workspace | null>(null);
  readonly objects = signal<WsObject[]>([]);

  readonly underlyingObjects = computed(() =>
    this.objects().filter((o) => UNDERLYING_KINDS.includes(o.kind)),
  );
  readonly currencyNames = computed(() =>
    this.objects()
      .filter((o) => o.kind === 'currency')
      .map((o) => o.name),
  );

  init(): void {
    if (this.loaded) {
      this.refreshObjects();
      return;
    }
    this.loaded = true;
    this.api.listWorkspaces().subscribe((list) => {
      if (list.length) this.useWorkspace(list[0]);
      else this.api.createWorkspace({ name: 'Default' }).subscribe((ws) => this.useWorkspace(ws));
    });
  }

  private useWorkspace(ws: Workspace): void {
    this.workspace.set(ws);
    this.refreshObjects();
  }

  refreshObjects(): void {
    const ws = this.workspace();
    if (!ws) return;
    this.api.listObjects(ws.id).subscribe((objs) => this.objects.set(objs));
  }

  //! a sensible default currency: the workspace currency if it exists among the objects,
  //! else the first available currency, else the workspace currency name.
  defaultCurrency(): string {
    const ws = this.workspace();
    if (!ws) return '';
    const ccys = this.currencyNames();
    return ccys.includes(ws.currency) ? ws.currency : (ccys[0] ?? ws.currency);
  }
}
