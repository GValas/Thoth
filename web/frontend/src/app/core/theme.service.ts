import { Injectable, signal } from '@angular/core';

export type ThemeMode = 'light' | 'dark';

const STORAGE_KEY = 'thoth.theme';

//! Switches between the refined-light charte and the Bloomberg-style dark terminal
//! charte by toggling a `theme-dark` class on <body>. All visual differences are
//! driven by CSS custom properties + a scoped Material colour override (see styles.scss
//! and _tokens.scss), so flipping the class re-skins the whole app instantly.
@Injectable({ providedIn: 'root' })
export class ThemeService {
  readonly mode = signal<ThemeMode>(this.initial());

  constructor() {
    this.apply(this.mode());
  }

  toggle(): void {
    this.set(this.mode() === 'dark' ? 'light' : 'dark');
  }

  set(mode: ThemeMode): void {
    this.mode.set(mode);
    this.apply(mode);
    try {
      localStorage.setItem(STORAGE_KEY, mode);
    } catch {
      /* storage unavailable — keep the in-memory mode only */
    }
  }

  private apply(mode: ThemeMode): void {
    document.body.classList.toggle('theme-dark', mode === 'dark');
  }

  private initial(): ThemeMode {
    try {
      const saved = localStorage.getItem(STORAGE_KEY);
      if (saved === 'dark' || saved === 'light') return saved;
    } catch {
      /* ignore */
    }
    return 'light';
  }
}
