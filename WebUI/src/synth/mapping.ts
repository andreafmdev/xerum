// Funzioni pure per convertire un valore normalizzato (0..1, come lo tiene
// l'APVTS) in unità reali e in etichette leggibili, secondo le formule
// descritte da parameters.json. Un gemello C++ con le stesse formule esiste
// in un task successivo: non "migliorare" le formule qui.

import { GROUPS, type ParamSpec } from "./params.generated";

const clamp01 = (v: number) => Math.min(1, Math.max(0, v));

export function denormalise(spec: ParamSpec, v: number): number {
  const m = spec.map;
  if (!m) return v;
  const x = clamp01(v);
  switch (m.type) {
    case "linear": return m.min + x * (m.max - m.min);
    case "log": return m.min * Math.pow(m.max / m.min, x);
    case "db": return x <= 0 ? -Infinity : 20 * Math.log10(x) + (m.offset ?? 0);
    case "ms-squared": return m.min + x * x * (m.max - m.min);
  }
}

export function normalise(spec: ParamSpec, real: number): number {
  const m = spec.map;
  if (!m) return real;
  switch (m.type) {
    case "linear": return clamp01((real - m.min) / (m.max - m.min));
    case "log": return clamp01(Math.log(real / m.min) / Math.log(m.max / m.min));
    case "db": return real === -Infinity ? 0 : clamp01(Math.pow(10, (real - (m.offset ?? 0)) / 20));
    case "ms-squared": return clamp01(Math.sqrt((real - m.min) / (m.max - m.min)));
  }
}

// Helper per parametri "choice": mappano l'indice dell'opzione sul range
// normalizzato 0..1 e viceversa.
export const toIndex = (spec: ParamSpec, v: number) => Math.round(clamp01(v) * ((spec.options?.length ?? 1) - 1));
export const fromIndex = (spec: ParamSpec, i: number) => { const n = spec.options?.length ?? 1; return n > 1 ? i / (n - 1) : 0; };

// Helper per parametri "int": il valore intero reale è denormalizzato tramite
// la stessa mappa lineare usata per i float.
export const toInt = (spec: ParamSpec, v: number) => Math.round(denormalise(spec, v));
export const fromInt = (spec: ParamSpec, n: number) => normalise(spec, n);

/** Intero con segno esplicito ("+3", "0", "-2"): usato dai formati e dagli stepper della UI. */
export const signedInt = (n: number) => (n > 0 ? `+${n}` : `${n}`);
const ARP_DIVS = ["1/32", "1/16", "1/8", "1/4"];

export function formatValue(spec: ParamSpec, v: number): string {
  if (spec.kind === "bool") return v >= 0.5 ? "On" : "Off";
  if (spec.kind === "choice") return spec.options?.[toIndex(spec, v)]?.label ?? "";
  if (spec.kind === "int") return signedInt(toInt(spec, v));
  const real = denormalise(spec, v);
  const d = spec.decimals ?? 0;
  switch (spec.labelKind) {
    case "hz": return real >= 1000 ? `${(real / 1000).toFixed(2)} kHz` : `${Math.round(real)} Hz`;
    case "time": return real >= 1000 ? `${(real / 1000).toFixed(2)} s` : `${Math.round(real)} ms`;
    case "pan": { const c = Math.round(real); return c === 0 ? "C" : c < 0 ? `${-c} L` : `${c} R`; }
    case "signed": return signedInt(Math.round(real));
    case "arp-rate": return ARP_DIVS[Math.min(3, Math.floor(clamp01(v) * 4))]!;
  }
  if (real === -Infinity) return "-inf";
  const text = real.toFixed(d);
  // I gradi si scrivono attaccati al numero ("180°"), ogni altra unità staccata.
  return spec.unit ? (spec.unit === "°" ? `${text}${spec.unit}` : `${text} ${spec.unit}`) : text;
}

export const paramLabel = (spec: ParamSpec) => `${GROUPS[spec.group] ?? spec.group} · ${spec.name}`;
