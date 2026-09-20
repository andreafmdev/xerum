import type { Tone } from "@xerum/ui";
import type { ModAssignment, ModSource } from "../juce/backend";
import { MOD_TARGETS, type ParamId } from "./params.generated";

export type { ModAssignment, ModSource };

// Unioni di stringhe dei parametri "choice" usati dalla matematica: devono
// combaciare con i `value` delle opzioni in params.generated.ts.
export type FilterType = "LP" | "HP" | "BP";
export type LfoShape = "Sine" | "Tri" | "Saw" | "Square" | "S&H";
/** Livello istantaneo di ogni sorgente: LFO bipolare -1..1, le altre 0..1. */
export type SourceLevels = Record<ModSource, number>;

/**
 * Il colore di ogni sorgente: e' cio' che distingue due anelli di modulazione sullo stesso knob,
 * quindi due sorgenti non possono condividerlo. `env2` prende "fx" e non "osc" — l'unico altro
 * tono libero — perche' i parametri modulati piu' spesso (wtpos, fine, level) stanno proprio nel
 * pannello dell'oscillatore, e li' un anello del colore della sezione si confonderebbe con essa.
 */
export const SOURCE_TONE: Record<ModSource, Tone> = { lfo: "lfo", env: "env", env2: "fx", vel: "master", mw: "filter" };
export const SOURCE_LABEL: Record<ModSource, string> = { lfo: "LFO", env: "ENV", env2: "ENV2", vel: "VEL", mw: "MW" };
export const MOD_SOURCES: ModSource[] = ["lfo", "env", "env2", "vel", "mw"];

/** Forma d'onda LFO a fase 0..1 (wrap), uscita -1..1. */
export function lfoShape(shape: LfoShape, phase: number): number {
  const ph = ((phase % 1) + 1) % 1;
  switch (shape) {
    case "Sine": return Math.sin(ph * Math.PI * 2);
    case "Tri": return 1 - 4 * Math.abs(ph - 0.5);
    case "Saw": return 1 - 2 * ph;
    case "Square": return ph < 0.5 ? 1 : -1;
    case "S&H": return Math.sin(Math.floor(ph * 8) * 7.3);
  }
}

const clamp01 = (v: number) => Math.min(1, Math.max(0, v));

/** Valore modulato istantaneo: somma di depth × livello sorgente, clampato. */
export function liveValue(value: number, mods: ModAssignment[], sources: SourceLevels): number {
  return clamp01(mods.reduce((acc, m) => acc + m.depth * sources[m.src], value));
}

/** Se il motore modula quel parametro: la lista generata da parameters.json (`modTarget: true`),
    la stessa di params::kModTargets. Un knob fuori lista non accetta il drop di una sorgente. */
export const isModTarget = (id: ParamId): boolean => (MOD_TARGETS as readonly string[]).includes(id);

export function modsFor(mods: ModAssignment[], target: ParamId): ModAssignment[] {
  return mods.filter((m) => m.target === target);
}
