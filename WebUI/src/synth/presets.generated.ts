// GENERATO da scripts/gen-params.mjs a partire da Source/parameters/presets.json.
// Non modificare a mano.
import type { ParamId } from "./params.generated";

export type Preset = { name: string; cat: string; values: Partial<Record<ParamId, number>> };

export const PRESETS: Preset[] = [
  { name: "Init", cat: "User", values: {} },
  { name: "Sub Pulse", cat: "Bass", values: {"wtIndex":1,"wtpos":0.1,"cutoff":0.35,"res":0.15,"att":0,"dec":0.3,"sus":0.6,"rel":0.2} },
  { name: "Reese Wide", cat: "Bass", values: {"wtIndex":0.2,"wtpos":0.45,"cutoff":0.4,"res":0.35,"drive":0.4} },
  { name: "Acid Line", cat: "Bass", values: {"wtIndex":0.2,"cutoff":0.3,"res":0.7,"keytrk":0.8,"dec":0.25,"sus":0.1} },
  { name: "Neon Lead", cat: "Lead", values: {"wtIndex":0.4,"wtpos":0.6,"cutoff":0.7,"res":0.25,"att":0.05} },
  { name: "Solid Saw", cat: "Lead", values: {"wtIndex":0.2,"wtpos":0,"cutoff":0.75,"res":0.1} },
  { name: "Glass Pad", cat: "Pad", values: {"wtIndex":0.8,"wtpos":0.5,"cutoff":0.55,"att":0.45,"rel":0.6,"sus":0.8} },
  { name: "Dust Choir", cat: "Pad", values: {"wtIndex":0.6,"wtpos":0.35,"cutoff":0.5,"att":0.5,"rel":0.65} },
  { name: "Velvet Keys", cat: "Keys", values: {"wtIndex":0.8,"wtpos":0.2,"cutoff":0.6,"dec":0.5,"sus":0.35,"rel":0.3} },
  { name: "Bell Tower", cat: "Keys", values: {"wtIndex":0.8,"wtpos":0.8,"cutoff":0.8,"dec":0.6,"sus":0.1,"rel":0.5} },
  { name: "Wire Pluck", cat: "Pluck", values: {"wtIndex":0.4,"cutoff":0.65,"res":0.3,"att":0,"dec":0.2,"sus":0,"rel":0.15} },
  { name: "Cold Sweep", cat: "FX", values: {"wtIndex":1,"wtpos":0.9,"cutoff":0.45,"res":0.5,"att":0.6,"rel":0.7} },
];
