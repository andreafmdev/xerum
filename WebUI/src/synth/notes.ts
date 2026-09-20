import type { NoteMask } from "../juce/backend";

/** La nota piu' acuta fra quelle accese nel mask, o null se non suona niente. */
export function topNote(mask: NoteMask): number | null {
  for (let w = 3; w >= 0; w--) {
    const word = mask[w] ?? 0;
    if (word === 0) continue;
    // `>>> 0` riporta a unsigned una parola con il bit 31 acceso, che come int32 sarebbe negativa
    // e farebbe sbagliare Math.clz32.
    return w * 32 + 31 - Math.clz32(word >>> 0);
  }
  return null;
}

/** Frequenza di una nota MIDI con il LA 69 a 440 Hz. */
export const noteHz = (note: number): number => 440 * Math.pow(2, (note - 69) / 12);
