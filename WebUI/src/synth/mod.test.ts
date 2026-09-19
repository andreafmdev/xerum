import { describe, expect, it } from "vitest";
import { lfoShape, liveValue, modsFor, MOD_SOURCES, SOURCE_LABEL, SOURCE_TONE, type ModAssignment } from "./mod";
import { addModPure } from "../juce/hooks";

describe("lfoShape", () => {
  it("sine", () => {
    expect(lfoShape("Sine", 0)).toBeCloseTo(0);
    expect(lfoShape("Sine", 0.25)).toBeCloseTo(1);
  });
  it("tri peaks at half", () => {
    expect(lfoShape("Tri", 0)).toBeCloseTo(-1);
    expect(lfoShape("Tri", 0.5)).toBeCloseTo(1);
  });
  it("saw falls", () => {
    expect(lfoShape("Saw", 0)).toBeCloseTo(1);
    expect(lfoShape("Saw", 0.999)).toBeCloseTo(-1);
  });
  it("square", () => {
    expect(lfoShape("Square", 0.2)).toBe(1);
    expect(lfoShape("Square", 0.7)).toBe(-1);
  });
  it("s&h is stepped and deterministic", () => {
    expect(lfoShape("S&H", 0.1)).toBe(lfoShape("S&H", 0.12));
    expect(lfoShape("S&H", 0.1)).not.toBe(lfoShape("S&H", 0.3));
  });
  it("wraps phase", () => expect(lfoShape("Saw", 1.25)).toBeCloseTo(lfoShape("Saw", 0.25)));
});

describe("le sorgenti del matrix", () => {
  it("ogni sorgente ha tono ed etichetta, e nessuna etichetta e' ripetuta", () => {
    for (const src of MOD_SOURCES) {
      expect(SOURCE_TONE[src]).toBeTruthy();
      expect(SOURCE_LABEL[src]).toBeTruthy();
    }
    expect(new Set(MOD_SOURCES.map((s) => SOURCE_LABEL[s])).size).toBe(MOD_SOURCES.length);
  });
});

describe("liveValue", () => {
  const sources = { lfo: 1, env: 0.5, env2: 0.25, vel: 0.7, mw: 0.5 };
  it("returns value without mods", () => expect(liveValue(0.4, [], sources)).toBe(0.4));
  it("lfo is bipolar depth × lfo", () => {
    expect(liveValue(0.4, [{ src: "lfo", target: "cutoff", depth: 0.2 }], { ...sources, lfo: -1 })).toBeCloseTo(0.2);
  });
  it("env adds depth × envelope level", () => {
    expect(liveValue(0.4, [{ src: "env", target: "cutoff", depth: 0.2 }], sources)).toBeCloseTo(0.5);
  });
  it("clamps to 0..1", () => {
    expect(liveValue(0.9, [{ src: "env", target: "cutoff", depth: 1 }], sources)).toBe(1);
  });
  it("env2 is a source of its own: same route, same depth, different level", () => {
    const viaEnv = liveValue(0.4, [{ src: "env", target: "cutoff", depth: 0.4 }], sources);
    const viaEnv2 = liveValue(0.4, [{ src: "env2", target: "cutoff", depth: 0.4 }], sources);
    expect(viaEnv).toBeCloseTo(0.6);
    expect(viaEnv2).toBeCloseTo(0.5);
  });
});

describe("addModPure / modsFor", () => {
  const mods: ModAssignment[] = [{ src: "lfo", target: "cutoff", depth: 0.25 }];
  it("adds a new assignment with default depth", () => {
    expect(addModPure(mods, "env", "wtpos")).toEqual([...mods, { src: "env", target: "wtpos", depth: 0.3 }]);
  });
  it("does not duplicate an existing pair", () => {
    expect(addModPure(mods, "lfo", "cutoff")).toBe(mods);
  });
  it("filters by target", () => {
    expect(modsFor(mods, "cutoff")).toHaveLength(1);
    expect(modsFor(mods, "res")).toHaveLength(0);
  });
  it("maps sources to tones", () => {
    expect(SOURCE_TONE).toEqual({ lfo: "lfo", env: "env", env2: "fx", vel: "master", mw: "filter" });
    // Nessuna sorgente puo' condividere il tono con un'altra: e' l'unica cosa che distingue due
    // anelli di modulazione sullo stesso knob.
    expect(new Set(Object.values(SOURCE_TONE)).size).toBe(MOD_SOURCES.length);
  });
});

// Stessi valori attesi del test C++ in Tests/LfoTests.cpp ("le forme d'onda coincidono con
// lfoShape di WebUI/src/synth/mod.ts"). Se una delle due implementazioni cambia, uno dei due
// test se ne accorge.
describe("parità con il DSP", () => {
  const PHASES = [0, 0.125, 0.25, 0.5, 0.75, 0.999];

  it("sine, tri, saw, square, S&H seguono le formule condivise", () => {
    for (const ph of PHASES) {
      expect(lfoShape("Sine", ph)).toBeCloseTo(Math.sin(ph * Math.PI * 2), 5);
      expect(lfoShape("Tri", ph)).toBeCloseTo(1 - 4 * Math.abs(ph - 0.5), 5);
      expect(lfoShape("Saw", ph)).toBeCloseTo(1 - 2 * ph, 5);
      expect(lfoShape("Square", ph)).toBeCloseTo(ph < 0.5 ? 1 : -1, 5);
      expect(lfoShape("S&H", ph)).toBeCloseTo(Math.sin(Math.floor(ph * 8) * 7.3), 5);
    }
  });
});
