import { lfoShape, type FilterType, type LfoShape } from "./mod";

/** Morph di frame: sine → saw → square → pulse lungo la posizione 0..1. Uscita -1..1. */
export function sampleWave(pos: number, t: number, warp: number): number {
  const ph = (t * (1 + warp * 3)) % 1;
  const sine = Math.sin(2 * Math.PI * ph);
  const saw = 2 * ph - 1;
  const sq = ph < 0.5 ? 1 : -1;
  const pl = ph < 0.2 ? 1 : -1;
  const s = pos * 3;
  if (s < 1) return sine * (1 - s) + saw * s;
  if (s < 2) return saw * (2 - s) + sq * (s - 1);
  return sq * (3 - s) + pl * (s - 2);
}

/** Risposta in frequenza stilizzata del filtro, come path SVG `M x y L x y …`. */
export function filterPath(cut: number, res: number, type: FilterType, W: number, H: number): string {
  const pts: string[] = [];
  for (let i = 0; i <= 60; i++) {
    const x = i / 60;
    const d = (x - cut) * 9;
    const peak = res * 0.9 * Math.exp(-d * d * 3);
    let g: number;
    if (type === "LP") g = d < 0 ? 1 + peak : Math.max(0, 1 + peak - d * 0.6);
    else if (type === "HP") g = d > 0 ? 1 + peak : Math.max(0, 1 + peak + d * 0.6);
    else g = Math.max(0, (0.25 + res * 0.9) * Math.exp(-d * d * (1.2 + res * 3)) + 0.05);
    pts.push(`${(x * W).toFixed(1)} ${(H - 6 - (Math.min(1.9, g) * (H - 14)) / 1.9).toFixed(1)}`);
  }
  return "M" + pts.join(" L");
}

/** Inviluppo ADSR con curve smussate; il sustain dura 0.35 unità. */
export function envPath(a: number, d: number, s: number, r: number, W: number, H: number): string {
  const t = a + d + 0.35 + r + 0.001;
  const x = (v: number) => 6 + (v / t) * (W - 12);
  const y = (v: number) => H - 6 - v * (H - 12);
  return (
    `M${x(0)} ${y(0)} C${x(a * 0.3)} ${y(0.9)},${x(a * 0.6)} ${y(1)},${x(a)} ${y(1)}` +
    ` C${x(a + d * 0.3)} ${y(s + (1 - s) * 0.3)},${x(a + d * 0.6)} ${y(s)},${x(a + d)} ${y(s)}` +
    ` L${x(a + d + 0.35)} ${y(s)}` +
    ` C${x(a + d + 0.35 + r * 0.3)} ${y(s * 0.3)},${x(a + d + 0.35 + r * 0.6)} ${y(0)},${x(t)} ${y(0)}`
  );
}

/** Due cicli della forma LFO, 101 punti. */
export function lfoPath(shape: LfoShape, W: number, H: number): string {
  let s = "";
  for (let i = 0; i <= 100; i++) {
    const x = 6 + (i / 100) * (W - 12);
    const y = H / 2 - lfoShape(shape, (i / 100) * 2) * (H / 2 - 8);
    s += (i ? " L" : "M") + x.toFixed(1) + " " + y.toFixed(1);
  }
  return s;
}

/** Magnitudo delle prime N armoniche del frame corrente, normalizzate 0..1 e scalate dal livello. */
export function spectrum(pos: number, warp: number, level: number, N: number): number[] {
  const M = 128;
  const frame = Array.from({ length: M }, (_, i) => sampleWave(pos, i / M, warp));
  const out: number[] = [];
  for (let h = 1; h <= N; h++) {
    let re = 0;
    let im = 0;
    for (let i = 0; i < M; i++) {
      const s = frame[i]!;
      re += s * Math.cos((2 * Math.PI * h * i) / M);
      im += s * Math.sin((2 * Math.PI * h * i) / M);
    }
    out.push(Math.min(1, (Math.hypot(re, im) / M) * 3) * level);
  }
  return out;
}
