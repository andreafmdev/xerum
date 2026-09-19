// Contratto TypeScript condiviso dai due backend (JuceBackend e FakeBackend):
// astrae l'accesso ai parametri, allo stato di modulazione/arp e ai meter,
// così l'UI e gli hook non dipendono da JUCE né da come viene simulato.

import { PARAM_SPECS, type ParamId, type ParamSpec } from "../synth/params.generated";
import { fromIndex, fromInt } from "../synth/mapping";

/**
 * Sorgente di modulazione disponibile nel mod matrix.
 *
 * `env` e' l'inviluppo d'ampiezza riusato come modulatore; `env2` e' il secondo inviluppo, che
 * non governa nessun volume. Stessi nomi di engine::ModSource in Source/engine/ModMatrix.h:
 * sono le stringhe che viaggiano nel nodo MODS dello stato, quindi devono coincidere.
 */
export type ModSource = "lfo" | "env" | "env2" | "vel" | "mw";
/** Assegnazione di una sorgente a un parametro target con una profondità -1..1. */
export type ModAssignment = { src: ModSource; target: ParamId; depth: number };

/** Stato condiviso host↔WebView: versione, mod matrix e i 16 step dell'arp. */
export type BridgeState = { version: number; mods: ModAssignment[]; arpSteps: number[] };
/**
 * Frame di meter inviato a 30 Hz dall'host (o simulato dal FakeBackend).
 *
 * `lfo`, `env`, `env2`, `vel`, `mw` sono i livelli istantanei delle cinque sorgenti del mod
 * matrix — gli stessi nomi di ModSource — ed è ciò che fa muovere gli anelli attorno ai knob
 * modulati (liveValue() in ../synth/mod.ts). Prima l'host mandava il solo `lfo` e le altre
 * quattro erano costanti scritte nella UI: l'anello mostrava la profondità giusta e il movimento
 * sbagliato. Il contratto sta in Source/engine/MeterFrame.h e in Source/bridge/MeterChannel.cpp.
 *
 * `env`, `env2` e `vel` arrivano già come **picco dell'ultimo frame**, non come valore
 * istantaneo: a 30 Hz un attacco di pochi millisecondi passerebbe fra due frame. `lfo` è
 * bipolare −1..1 e `mw` è una posizione, quindi quei due sono istantanei.
 */
export type MeterFrame = {
  in: number;
  out: number;
  lfo: number;
  env: number;
  env2: number;
  vel: number;
  mw: number;
  arpStep: number;
};

/** Frame a zero: valore iniziale di useMeters e base da cui i test costruiscono i loro frame. */
export const ZERO_METERS: MeterFrame = { in: 0, out: 0, lfo: 0, env: 0, env2: 0, vel: 0, mw: 0, arpStep: 0 };

/** Un singolo parametro, come esposto all'UI: lettura/scrittura normalizzata 0..1 e gesture per l'automazione host. */
export interface ParamHandle {
  get(): number;                 // normalizzato 0..1 (bool → 0/1, int → (n-min)/(max-min), choice → i/(n-1))
  set(v: number): void;
  begin(): void; end(): void;    // gesture (no-op per bool/choice)
  subscribe(cb: () => void): () => void;
  readonly orphan: boolean;      // id sconosciuto al backend
}

/** Contratto implementato sia da JuceBackend (task 10) sia da FakeBackend. */
export interface Backend {
  readonly kind: "juce" | "fake";
  param(id: ParamId): ParamHandle;
  getState(): Promise<BridgeState>;
  setMods(mods: ModAssignment[], origin: string): Promise<void>;
  setArpSteps(steps: number[], origin: string): Promise<void>;
  loadPreset(index: number): Promise<void>;
  onStateChanged(cb: (s: BridgeState & { origin: string }) => void): () => void;
  onMeters(cb: (m: MeterFrame) => void): () => void;
}

/** Default normalizzato di uno spec (float: già 0..1; bool: 0/1; int: mappato; choice: indice mappato). */
export function defaultNormalised(spec: ParamSpec): number {
  switch (spec.kind) {
    case "bool": return spec.default ? 1 : 0;
    case "int": return fromInt(spec, Number(spec.default));
    case "choice": return fromIndex(spec, Number(spec.default));
    default: return Number(spec.default);
  }
}
export const specOf = (id: ParamId): ParamSpec => PARAM_SPECS[id];
