export type { Preset } from "./presets.generated";
export { PRESETS } from "./presets.generated";
import type { Preset } from "./presets.generated";

export const CATEGORIES = ["All", "Bass", "Lead", "Pad", "Keys", "Pluck", "FX", "User"];

export function filterPresets(list: Preset[], cat: string, query: string): Preset[] {
  const q = query.trim().toLowerCase();
  return list.filter((p) => (cat === "All" || p.cat === cat) && p.name.toLowerCase().includes(q));
}

/** Indice ciclico. */
export function step(index: number, delta: number, count: number): number {
  return (((index + delta) % count) + count) % count;
}
