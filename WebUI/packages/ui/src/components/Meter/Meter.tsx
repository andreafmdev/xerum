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

/** Barra di LED orizzontale: verde di sezione, gialla sopra i 3/4, rossa in cima. */
export function Meter({ level, label, segments = 24, tone, className }: MeterProps) {
  const lit = litSegments(level, segments);
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
      {Array.from({ length: segments }, (_, i) => {
        const zone = zoneOf(i, segments);
        const on = i < lit;
        return (
          <span
            key={i}
            data-testid="meter-segment"
            data-lit={on}
            data-zone={zone}
            className={cn(
              "h-2 w-1 rounded-[1px] transition-colors duration-75",
              !on && "bg-led-off",
              on && zone === "ok" && "bg-(--tone) shadow-[0_0_4px_var(--tone)]",
              on && zone === "warn" && "bg-warning shadow-[0_0_4px_var(--color-warning)]",
              on && zone === "clip" && "bg-destructive shadow-[0_0_4px_var(--destructive)]",
            )}
          />
        );
      })}
    </div>
  );
}
