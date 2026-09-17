import type { CSSProperties } from "react";

export type Tone = "osc" | "filter" | "env" | "lfo" | "fx" | "master";

export const TONES: readonly Tone[] = ["osc", "filter", "env", "lfo", "fx", "master"];

/**
 * Stile inline che imposta la variabile `--tone` sul colore di sezione.
 * I figli la consumano con `text-(--tone)`, `bg-(--tone)`, `stroke-(--tone)`.
 * Senza `tone` ritorna undefined: il componente eredita `--tone` dal Panel o dal default `:root`.
 */
export function toneStyle(tone?: Tone): CSSProperties | undefined {
  if (!tone) return undefined;
  return { "--tone": `var(--color-${tone})` } as CSSProperties;
}
