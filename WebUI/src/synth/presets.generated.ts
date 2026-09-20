// GENERATO da scripts/gen-params.mjs a partire da Source/parameters/presets.json.
// Non modificare a mano.
import type { ParamId } from "./params.generated";

export type Preset = { name: string; cat: string; values: Partial<Record<ParamId, number>> };

export const PRESETS: Preset[] = [
  { name: "Init", cat: "User", values: {} },
  { name: "Sub Pulse", cat: "Bass", values: {"wtIndex":0.4166666666666667,"wtpos":0.1,"oct":0.3333,"ftype":0,"slope":1,"cutoff":0.392,"res":0.25,"drive":0.3464,"keytrk":0.6,"att":0,"dec":0.25,"sus":0.75,"rel":0.078,"level":0.9,"volume":0.8} },
  { name: "Reese Wide", cat: "Bass", values: {"wtIndex":0.08333333333333333,"wtpos":0.45,"oct":0.3333,"ftype":0,"slope":1,"cutoff":0.434,"res":0.35,"drive":0.5,"keytrk":0.5,"att":0,"dec":0.316,"sus":0.8,"rel":0.099,"level":0.6,"volume":0.8} },
  { name: "Acid Line", cat: "Bass", values: {"wtIndex":0.08333333333333333,"wtpos":0,"oct":0.3333,"ftype":0,"slope":0,"cutoff":0.333,"res":0.78,"drive":0.4472,"keytrk":0.9,"att":0,"dec":0.193,"sus":0.1,"rel":0.049,"level":0.8,"volume":0.8} },
  { name: "Neon Lead", cat: "Lead", values: {"wtIndex":0.16666666666666666,"wtpos":0.6,"ftype":0,"slope":1,"cutoff":0.693,"res":0.3,"drive":0.3873,"keytrk":0.5,"att":0.022,"dec":0.25,"sus":0.85,"rel":0.122,"level":0.8,"volume":0.8} },
  { name: "Solid Saw", cat: "Lead", values: {"wtIndex":0.08333333333333333,"wtpos":0,"ftype":0,"slope":1,"cutoff":0.784,"res":0.15,"drive":0,"keytrk":0.5,"att":0.022,"dec":0.25,"sus":0.9,"rel":0.099,"level":0.85,"volume":0.8} },
  { name: "Glass Pad", cat: "Pad", values: {"wtIndex":0.3333333333333333,"wtpos":0.5,"ftype":0,"slope":1,"cutoff":0.593,"res":0.2,"drive":0,"keytrk":0.4,"att":0.387,"dec":0.474,"sus":0.8,"rel":0.559,"level":0.7,"volume":0.7} },
  { name: "Dust Choir", cat: "Pad", values: {"wtIndex":0.25,"wtpos":0.35,"ftype":0,"slope":1,"cutoff":0.534,"res":0.2,"drive":0,"keytrk":0.4,"att":0.316,"dec":0.474,"sus":0.75,"rel":0.474,"level":0.7,"volume":0.7} },
  { name: "Velvet Keys", cat: "Keys", values: {"wtIndex":0.3333333333333333,"wtpos":0.2,"ftype":0,"slope":1,"cutoff":0.634,"res":0.15,"drive":0,"keytrk":0.6,"att":0.022,"dec":0.25,"sus":0.35,"rel":0.193,"level":0.85,"volume":0.8} },
  { name: "Bell Tower", cat: "Keys", values: {"wtIndex":0.3333333333333333,"wtpos":0.8,"ftype":0,"slope":0,"cutoff":0.826,"res":0.1,"drive":0,"keytrk":0.8,"att":0,"dec":0.387,"sus":0.05,"rel":0.316,"level":0.8,"volume":0.8} },
  { name: "Wire Pluck", cat: "Pluck", values: {"wtIndex":0.16666666666666666,"wtpos":0.25,"ftype":0,"slope":1,"cutoff":0.735,"res":0.35,"drive":0.3162,"keytrk":0.7,"att":0,"dec":0.158,"sus":0,"rel":0.078,"level":0.9,"volume":0.8} },
  { name: "Cold Sweep", cat: "FX", values: {"wtIndex":0.4166666666666667,"wtpos":0.9,"ftype":0,"slope":1,"cutoff":0.492,"res":0.55,"drive":0,"keytrk":0.3,"att":0.474,"dec":0.559,"sus":0.7,"rel":0.661,"level":0.7,"volume":0.7} },
];
