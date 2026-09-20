export type { Preset } from "./presets.generated";
export { PRESETS } from "./presets.generated";
import { defaultNormalised } from "../juce/backend";
import { PARAM_SPECS } from "./params.generated";
import type { Preset } from "./presets.generated";
import { WAVETABLES } from "./wavetables.generated";

export const CATEGORIES = ["All", "Bass", "Lead", "Pad", "Keys", "Pluck", "FX", "User"];

/** Tutti i preset sono di fabbrica: una banca e un autore soli, non nomi inventati. */
export const BANK = "Factory";
export const AUTHOR = "Xerum";

/** Cosa aspettarsi da un preset di quella categoria: il testo del riquadro di anteprima. */
export const DESCRIPTIONS: Record<string, string> = {
  Bass: "Basso compatto con filtro in movimento; il warp aggiunge grinta sulle note basse.",
  Lead: "Lead brillante, unison largo, LFO lento sulla posizione della wavetable.",
  Pad: "Pad evolutivo: attacco lungo, chorus e riverbero ampio, movimento lento del filtro.",
  Keys: "Timbro percussivo e definito, decadimento naturale, poco effetto.",
  Pluck: "Pluck corto con envelope rapida sul cutoff; ideale per arpeggi.",
  FX: "Texture in movimento, pensata per transizioni e riser.",
  User: "Patch iniziale: oscillatore singolo, filtro aperto, nessun effetto.",
};

/** Posizione, warp e tavola con cui disegnare la miniatura dell'onda: i valori del preset, o i
    default di spec per cio' che il preset non tocca (Init non tocca niente). */
export function presetWave(p: Preset): { pos: number; warp: number; frames: number[][] } {
  const wt = p.values.wtIndex ?? defaultNormalised(PARAM_SPECS.wtIndex);
  return {
    pos: p.values.wtpos ?? defaultNormalised(PARAM_SPECS.wtpos),
    warp: p.values.warp ?? defaultNormalised(PARAM_SPECS.warp),
    frames: (WAVETABLES[Math.round(wt * (WAVETABLES.length - 1))] ?? WAVETABLES[0]!).frames,
  };
}

export function filterPresets(list: Preset[], cat: string, query: string): Preset[] {
  const q = query.trim().toLowerCase();
  return list.filter((p) => (cat === "All" || p.cat === cat) && p.name.toLowerCase().includes(q));
}

/** Indice ciclico. */
export function step(index: number, delta: number, count: number): number {
  return (((index + delta) % count) + count) % count;
}
