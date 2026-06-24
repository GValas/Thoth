//! Divergent blue -> white -> red heatmap. For premium-like (unsigned) metrics the
//! domain is [min,max] and low=blue, high=red. For signed Greeks the domain is centred
//! on zero (blue=positive, red=negative) so sign reads at a glance.

const LOW = [37, 99, 235]; // #2563EB blue
const MID = [255, 255, 255]; // white
const HIGH = [220, 38, 38]; // #DC2626 red

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

//! Premium / unsigned metric: blue(min) -> white(mid) -> red(max).
export function divergentScale(values: number[]): HeatScale {
  const finite = values.filter((v) => Number.isFinite(v));
  const min = finite.length ? Math.min(...finite) : 0;
  const max = finite.length ? Math.max(...finite) : 1;
  const span = max - min || 1;
  return {
    bg(value) {
      if (value == null || !Number.isFinite(value)) return 'transparent';
      const t = (value - min) / span; // 0..1
      return t <= 0.5
        ? colorAt(t / 0.5, LOW, MID)
        : colorAt((t - 0.5) / 0.5, MID, HIGH);
    },
  };
}

//! Signed Greek: centred on zero, blue = positive, red = negative.
export function signedScale(values: number[]): HeatScale {
  const finite = values.filter((v) => Number.isFinite(v));
  const mag = finite.length ? Math.max(...finite.map((v) => Math.abs(v)), 1e-12) : 1;
  return {
    bg(value) {
      if (value == null || !Number.isFinite(value)) return 'transparent';
      const t = value / mag; // -1..1
      // positive -> blue, negative -> red
      return t >= 0 ? colorAt(t, MID, LOW) : colorAt(-t, MID, HIGH);
    },
  };
}

//! Text colour with adequate contrast against a heat-cell background.
export function textOn(value: number | null | undefined, max: number): string {
  if (value == null || !Number.isFinite(value)) return 'var(--thoth-text)';
  return Math.abs(value) / (max || 1) > 0.6 ? '#ffffff' : 'var(--thoth-text)';
}
