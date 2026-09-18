/** Formattatori valore → testo. Input sempre normalizzato 0..1 salvo gli interi degli stepper. */

const dbOf = (v: number, offset = 0) => (v <= 0 ? "-inf" : `${(20 * Math.log10(v) + offset).toFixed(1)} dB`);

export const fmt = {
  pct: (v: number) => `${Math.round(v * 100)} %`,
  frame: (v: number) => (1 + v * 63).toFixed(1),
  cents: (v: number) => `${Math.round(v * 100)} ct`,
  fine: (v: number) => `${Math.round((v - 0.5) * 200)} ct`,
  db: (v: number) => dbOf(v),
  volume: (v: number) => dbOf(v, 6),
  hz: (v: number) => {
    const hz = 20 * Math.pow(1000, v);
    return hz >= 1000 ? `${(hz / 1000).toFixed(2)} kHz` : `${Math.round(hz)} Hz`;
  },
  drive: (v: number) => `${(v * 24).toFixed(1)} dB`,
  pan: (v: number) => {
    const c = Math.round((v - 0.5) * 100);
    return c === 0 ? "C" : c < 0 ? `${-c} L` : `${c} R`;
  },
  glide: (v: number) => `${Math.round(v * v * 2000)} ms`,
  envMs: (v: number) => {
    const m = 1 + v * v * 8000;
    return m >= 1000 ? `${(m / 1000).toFixed(2)} s` : `${Math.round(m)} ms`;
  },
  curve: (v: number) => fmt.signedInt(Math.round((v - 0.5) * 200)),
  lfoRate: (v: number, sync: boolean) =>
    sync ? ["1/16", "1/8", "1/4", "1/2", "1", "2"][Math.min(5, Math.floor(v * 6))]! : `${(0.05 * Math.pow(400, v)).toFixed(2)} Hz`,
  deg: (v: number) => `${Math.round(v * 360)}°`,
  fadeMs: (v: number) => `${Math.round(v * 4000)} ms`,
  chorusHz: (v: number) => `${(0.1 + v * 5).toFixed(2)} Hz`,
  arpRate: (v: number) => ["1/32", "1/16", "1/8", "1/4"][Math.min(3, Math.floor(v * 4))]!,
  octaves: (v: number) => String(1 + Math.round(v * 3)),
  signedInt: (v: number) => (v > 0 ? `+${v}` : `${v}`),
};
