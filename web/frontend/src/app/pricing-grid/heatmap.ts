//! Divergent low -> mid -> high heatmap. For premium-like (unsigned) metrics the
//! domain is [min,max] and low=blue, high=red. For signed Greeks the domain is centred
//! on zero (blue=positive, red=negative) so sign reads at a glance.
//!
//! Stops are read from the active theme's CSS custom properties so the scale follows
//! the light/dark charte — in dark mode the mid stop is the (dark) grid surface, so
//! low-magnitude cells melt into the grid instead of glowing white.

const FALLBACK_LOW = [37, 99, 235]; // #2563EB blue
const FALLBACK_MID = [255, 255, 255]; // white
const FALLBACK_HIGH = [220, 38, 38]; // #DC2626 red

function parseColor(raw: string, fallback: number[]): number[] {
  const s = raw.trim();
  const hex = s.match(/^#([0-9a-f]{3}|[0-9a-f]{6})$/i);
  if (hex) {
    let h = hex[1];
    if (h.length === 3) h = h[0] + h[0] + h[1] + h[1] + h[2] + h[2];
    return [parseInt(h.slice(0, 2), 16), parseInt(h.slice(2, 4), 16), parseInt(h.slice(4, 6), 16)];
  }
  const rgb = s.match(/(\d+)[,\s]+(\d+)[,\s]+(\d+)/);
  if (rgb) return [Number(rgb[1]), Number(rgb[2]), Number(rgb[3])];
  return fallback;
}

//! Snapshot the three divergent stops from the current theme.
function themeStops(): { low: number[]; mid: number[]; high: number[] } {
  if (typeof getComputedStyle === 'undefined') {
    return { low: FALLBACK_LOW, mid: FALLBACK_MID, high: FALLBACK_HIGH };
  }
  const cs = getComputedStyle(document.body);
  return {
    low: parseColor(cs.getPropertyValue('--thoth-heat-low'), FALLBACK_LOW),
    mid: parseColor(cs.getPropertyValue('--thoth-heat-mid'), FALLBACK_MID),
    high: parseColor(cs.getPropertyValue('--thoth-heat-high'), FALLBACK_HIGH),
  };
}

function lerp(a: number, b: number, t: number): number {
  return Math.round(a + (b - a) * t);
}

function colorAt(t: number, lo: number[], hi: number[]): string {
  const c = [lerp(lo[0], hi[0], t), lerp(lo[1], hi[1], t), lerp(lo[2], hi[2], t)];
  return `rgb(${c[0]}, ${c[1]}, ${c[2]})`;
}

export interface HeatScale {
  bg(value: number | null | undefined): string;
}

//! Premium / unsigned metric: low(min) -> mid -> high(max).
export function divergentScale(values: number[]): HeatScale {
  const finite = values.filter((v) => Number.isFinite(v));
  const min = finite.length ? Math.min(...finite) : 0;
  const max = finite.length ? Math.max(...finite) : 1;
  const span = max - min || 1;
  const { low, mid, high } = themeStops();
  return {
    bg(value) {
      if (value == null || !Number.isFinite(value)) return 'transparent';
      const t = (value - min) / span; // 0..1
      return t <= 0.5
        ? colorAt(t / 0.5, low, mid)
        : colorAt((t - 0.5) / 0.5, mid, high);
    },
  };
}

//! Signed Greek: centred on zero, low(blue) = positive, high(red) = negative.
export function signedScale(values: number[]): HeatScale {
  const finite = values.filter((v) => Number.isFinite(v));
  const mag = finite.length ? Math.max(...finite.map((v) => Math.abs(v)), 1e-12) : 1;
  const { low, mid, high } = themeStops();
  return {
    bg(value) {
      if (value == null || !Number.isFinite(value)) return 'transparent';
      const t = value / mag; // -1..1
      // positive -> low (blue), negative -> high (red)
      return t >= 0 ? colorAt(t, mid, low) : colorAt(-t, mid, high);
    },
  };
}

//! Text colour with adequate contrast against a heat-cell background.
export function textOn(value: number | null | undefined, max: number): string {
  if (value == null || !Number.isFinite(value)) return 'var(--thoth-text)';
  return Math.abs(value) / (max || 1) > 0.6 ? '#ffffff' : 'var(--thoth-text)';
}
