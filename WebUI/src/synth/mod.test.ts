import { describe, expect, it } from "vitest";
import { lfoHz, lfoShape, liveValue, addMod, modsFor, SOURCE_TONE, type ModAssignment } from "./mod";

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

describe("lfoHz", () => {
  it("free: 0.05 Hz .. 20 Hz", () => {
    expect(lfoHz(0, false)).toBeCloseTo(0.05);
    expect(lfoHz(1, false)).toBeCloseTo(20);
  });
  it("synced: 6 note divisions from 1/16 to 2 bars at 120 bpm", () => {
    expect(lfoHz(0, true)).toBe(4);
    expect(lfoHz(1, true)).toBe(0.125);
  });
});

describe("liveValue", () => {
  const sources = { lfo: 1, env: 0.5, vel: 0.7, mw: 0.5 };
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
});

describe("addMod / modsFor", () => {
  const mods: ModAssignment[] = [{ src: "lfo", target: "cutoff", depth: 0.25 }];
  it("adds a new assignment with default depth", () => {
    expect(addMod(mods, "env", "wtpos")).toEqual([...mods, { src: "env", target: "wtpos", depth: 0.3 }]);
  });
  it("does not duplicate an existing pair", () => {
    expect(addMod(mods, "lfo", "cutoff")).toBe(mods);
  });
  it("filters by target", () => {
    expect(modsFor(mods, "cutoff")).toHaveLength(1);
    expect(modsFor(mods, "res")).toHaveLength(0);
  });
  it("maps sources to tones", () => {
    expect(SOURCE_TONE).toEqual({ lfo: "lfo", env: "env", vel: "master", mw: "filter" });
  });
});
