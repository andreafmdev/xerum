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

/** Punto di controllo di una Bézier: frazione lungo il segmento e frazione della corsa in altezza. */
type Ctrl = readonly [number, number];
const lerp = (a: number, b: number, k: number) => a + (b - a) * k;

/**
 * I due punti di controllo di un segmento in base a `curve` (-1..1): a 0 gli stessi di sempre
 * (esponenziale), a +1 sulla corda (una retta), a -1 piegati di più (ginocchio anticipato), come
 * fa dsp::ADSREnvelope. `rise` è la frazione di corsa percorsa (1 per un segmento intero).
 */
function bend(curve: number, soft: readonly [Ctrl, Ctrl]): [Ctrl, Ctrl] {
  const straight: [Ctrl, Ctrl] = [[1 / 3, 1 / 3], [2 / 3, 2 / 3]];
  const sharp: [Ctrl, Ctrl] = [[0.12, 0.97], [0.35, 1]];
  const to = curve >= 0 ? straight : sharp;
  const k = Math.min(1, Math.abs(curve));
  return [
    [lerp(soft[0][0], to[0][0], k), lerp(soft[0][1], to[0][1], k)],
    [lerp(soft[1][0], to[1][0], k), lerp(soft[1][1], to[1][1], k)],
  ];
}

/** Inviluppo ADSR con curve smussate; il sustain dura 0.35 unità. `curve` come `envCurve` (-1..1). */
export function envPath(a: number, d: number, s: number, r: number, W: number, H: number, curve = 0): string {
  const t = a + d + 0.35 + r + 0.001;
  const x = (v: number) => 6 + (v / t) * (W - 12);
  const y = (v: number) => H - 6 - v * (H - 12);
  const [a1, a2] = bend(curve, [[0.3, 0.9], [0.6, 1]]);
  const [d1, d2] = bend(curve, [[0.3, 0.7], [0.6, 1]]);
  const [r1, r2] = bend(curve, [[0.3, 0.7], [0.6, 1]]);
  // Decay scende da 1 a s, release da s a 0: la frazione di corsa si traduce in altezza assoluta.
  const dy = (f: number) => 1 - (1 - s) * f;
  const ry = (f: number) => s * (1 - f);
  const rel = a + d + 0.35;
  return (
    `M${x(0)} ${y(0)} C${x(a * a1[0])} ${y(a1[1])},${x(a * a2[0])} ${y(a2[1])},${x(a)} ${y(1)}` +
    ` C${x(a + d * d1[0])} ${y(dy(d1[1]))},${x(a + d * d2[0])} ${y(dy(d2[1]))},${x(a + d)} ${y(s)}` +
    ` L${x(rel)} ${y(s)}` +
    ` C${x(rel + r * r1[0])} ${y(ry(r1[1]))},${x(rel + r * r2[0])} ${y(ry(r2[1]))},${x(t)} ${y(0)}`
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
