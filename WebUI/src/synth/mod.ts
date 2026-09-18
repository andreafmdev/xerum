import type { Tone } from "@xerum/ui";
import type { KnobId, LfoShape } from "./params";

export type ModSource = "lfo" | "env" | "vel" | "mw";
export type ModAssignment = { src: ModSource; target: KnobId; depth: number };
/** Livello istantaneo di ogni sorgente: LFO bipolare -1..1, le altre 0..1. */
export type SourceLevels = Record<ModSource, number>;

export const SOURCE_TONE: Record<ModSource, Tone> = { lfo: "lfo", env: "env", vel: "master", mw: "filter" };
export const SOURCE_LABEL: Record<ModSource, string> = { lfo: "LFO", env: "ENV", vel: "VEL", mw: "MW" };
export const MOD_SOURCES: ModSource[] = ["lfo", "env", "vel", "mw"];
export const DEFAULT_DEPTH = 0.3;

const SYNC_HZ = [4, 2, 1, 0.5, 0.25, 0.125];

/** Frequenza LFO: libera 0.05..20 Hz, oppure 6 divisioni di nota a 120 bpm. */
export function lfoHz(rate: number, sync: boolean): number {
  return sync ? SYNC_HZ[Math.min(5, Math.floor(rate * 6))]! : 0.05 * Math.pow(400, rate);
}

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

export function modsFor(mods: ModAssignment[], target: KnobId): ModAssignment[] {
  return mods.filter((m) => m.target === target);
}

/** Aggiunge un'assegnazione; identità invariata se la coppia esiste già. */
export function addMod(mods: ModAssignment[], src: ModSource, target: KnobId): ModAssignment[] {
  return mods.some((m) => m.src === src && m.target === target) ? mods : [...mods, { src, target, depth: DEFAULT_DEPTH }];
}
