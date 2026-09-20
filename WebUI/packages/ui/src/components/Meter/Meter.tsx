import { memo } from "react";
import { cn } from "@/lib/utils";
import { toneStyle, type Tone } from "@/lib/tone";

export type MeterProps = {
  /** Livello 0..1. */
  level: number;
  /** Nome accessibile. */
  label: string;
  /** Numero di LED. Default 24. */
  segments?: number;
  tone?: Tone;
  className?: string;
};

export function litSegments(level: number, segments: number): number {
  const l = Math.min(1, Math.max(0, level));
  return Math.round(l * segments);
}

type Zone = "ok" | "warn" | "clip";
const zoneOf = (i: number, n: number): Zone => (i >= n * 0.9 ? "clip" : i >= n * 0.75 ? "warn" : "ok");

// La zona di ogni LED dipende solo da (indice, numero di LED) e la classe solo da (acceso, zona):
// calcolate una volta per numero di LED, non 24 volte per frame per ciascun meter.
const ZONES = new Map<number, Zone[]>();
const zonesFor = (n: number): Zone[] => {
  let z = ZONES.get(n);
  if (!z) {
    z = Array.from({ length: n }, (_, i) => zoneOf(i, n));
    ZONES.set(n, z);
  }
  return z;
};
// 70ms (il token press) invece di 75: impercettibile su un LED, ed è un valore del vocabolario.
const LED = "h-2 w-1 rounded-[1px] transition-colors duration-(--dur-press)";
const LED_OFF = `${LED} bg-led-off`;
const LED_ON: Record<Zone, string> = {
  ok: `${LED} bg-(--tone) shadow-[0_0_4px_var(--tone)]`,
  warn: `${LED} bg-warning shadow-[0_0_4px_var(--color-warning)]`,
  clip: `${LED} bg-destructive shadow-[0_0_4px_var(--destructive)]`,
};

/** Barra di LED orizzontale: verde di sezione, gialla sopra i 3/4, rossa in cima.
    Memoizzata: i frame arrivano a 30 Hz anche quando il livello non cambia. */
export const Meter = memo(function Meter({ level, label, segments = 24, tone, className }: MeterProps) {
  const lit = litSegments(level, segments);
  const zones = zonesFor(segments);
  return (
    <div
      role="meter"
      aria-label={label}
      aria-valuemin={0}
      aria-valuemax={1}
      aria-valuenow={Math.min(1, Math.max(0, level))}
      data-slot="meter"
      className={cn("inline-flex items-center gap-0.5", className)}
      style={toneStyle(tone)}
    >
      {zones.map((zone, i) => {
        const on = i < lit;
        return <span key={i} data-testid="meter-segment" data-lit={on} data-zone={zone} className={on ? LED_ON[zone] : LED_OFF} />;
      })}
    </div>
  );
});
