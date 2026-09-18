export type Preset = { name: string; cat: string };

export const CATEGORIES = ["All", "Bass", "Lead", "Pad", "Keys", "Pluck", "FX", "User"];

export const PRESETS: Preset[] = (
  [
    ["Init", "User"], ["Glass Pad", "Pad"], ["Sub Pulse", "Bass"], ["Neon Lead", "Lead"], ["Velvet Keys", "Keys"],
    ["Warp Pluck", "Pluck"], ["Riser 01", "FX"], ["Dust Choir", "Pad"], ["Acid Line", "Bass"], ["Bell Tower", "Keys"],
    ["Shimmer Air", "Pad"], ["Formant Talk", "Lead"], ["Wire Pluck", "Pluck"], ["Deep Drone", "FX"], ["Solid Saw", "Lead"],
    ["Reese Wide", "Bass"], ["Music Box", "Keys"], ["Cold Sweep", "FX"],
  ] as const
).map(([name, cat]) => ({ name, cat }));

export function filterPresets(list: Preset[], cat: string, query: string): Preset[] {
  const q = query.trim().toLowerCase();
  return list.filter((p) => (cat === "All" || p.cat === cat) && p.name.toLowerCase().includes(q));
}

/** Indice ciclico. */
export function step(index: number, delta: number, count: number): number {
  return (((index + delta) % count) + count) % count;
}
