/** Angoli in gradi nel sistema SVG (0° = destra, y verso il basso, senso orario). */
export const KNOB_START = 135;
export const KNOB_SWEEP = 270;
const KNOB_CENTRE = KNOB_START + KNOB_SWEEP / 2; // 270 = in alto

export function knobAngles(value: number, bipolar: boolean): { start: number; end: number } {
  const at = KNOB_START + KNOB_SWEEP * value;
  if (!bipolar) return { start: KNOB_START, end: at };
  return at >= KNOB_CENTRE ? { start: KNOB_CENTRE, end: at } : { start: at, end: KNOB_CENTRE };
}

/** Punto sul cerchio (cx, cy, r) all'angolo `deg`, arrotondato a 3 decimali. */
export function polar(cx: number, cy: number, r: number, deg: number): [number, number] {
  const a = (deg * Math.PI) / 180;
  return [round(cx + r * Math.cos(a)), round(cy + r * Math.sin(a))];
}

const round = (n: number) => Math.round(n * 1000) / 1000 + 0;

export function arcPath(cx: number, cy: number, r: number, startDeg: number, endDeg: number): string {
  if (endDeg < startDeg) [startDeg, endDeg] = [endDeg, startDeg];
  const [x1, y1] = polar(cx, cy, r, startDeg);
  const [x2, y2] = polar(cx, cy, r, endDeg);
  const large = endDeg - startDeg > 180 ? 1 : 0;
  return `M ${x1} ${y1} A ${r} ${r} 0 ${large} 1 ${x2} ${y2}`;
}
