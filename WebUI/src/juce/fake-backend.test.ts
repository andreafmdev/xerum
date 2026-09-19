import { describe, expect, it, vi } from "vitest";
import { FakeBackend } from "./fake-backend";
import { isNoteActive, noteMaskOf, ZERO_METERS } from "./backend";
import { PARAM_SPECS } from "../synth/params.generated";
import { PRESETS } from "../synth/presets.generated";

describe("FakeBackend params", () => {
  it("starts every param at its spec default, normalised", () => {
    const b = new FakeBackend();
    expect(b.param("cutoff").get()).toBeCloseTo(0.62);
    expect(b.param("oscOn").get()).toBe(1);
    expect(b.param("slope").get()).toBe(1);      // choice index 1 of 2 → 1
    expect(b.param("oct").get()).toBe(0.5);      // int 0 in -3..3
  });
  it("set notifies subscribers and logs gestures in order", () => {
    const b = new FakeBackend();
    const h = b.param("cutoff");
    const cb = vi.fn();
    h.subscribe(cb);
    h.begin(); h.set(0.7); h.end();
    expect(cb).toHaveBeenCalledTimes(1);
    expect(h.get()).toBe(0.7);
    expect(b.log).toEqual([{ id: "cutoff", op: "begin" }, { id: "cutoff", op: "set", v: 0.7 }, { id: "cutoff", op: "end" }]);
  });
  it("clamps to 0..1 and unsubscribes", () => {
    const b = new FakeBackend();
    const h = b.param("res");
    const cb = vi.fn();
    const off = h.subscribe(cb);
    h.set(2); expect(h.get()).toBe(1);
    off(); h.set(0.1); expect(cb).toHaveBeenCalledTimes(1);
  });
  it("returns the same handle for the same id", () => {
    const b = new FakeBackend();
    expect(b.param("res")).toBe(b.param("res"));
  });
});

describe("FakeBackend state and events", () => {
  it("getState returns defaults or the injected state", async () => {
    expect((await new FakeBackend().getState()).mods).toEqual([]);
    const b = new FakeBackend({ state: { mods: [{ src: "lfo", target: "cutoff", depth: 0.25 }] } });
    expect((await b.getState()).mods).toHaveLength(1);
  });
  it("setMods persists and does not echo to the same origin", async () => {
    const b = new FakeBackend();
    const cb = vi.fn();
    b.onStateChanged(cb);
    await b.setMods([{ src: "env", target: "wtpos", depth: 0.3 }], "me");
    expect((await b.getState()).mods).toHaveLength(1);
    expect(cb).toHaveBeenCalledWith(expect.objectContaining({ origin: "me" }));
  });
  it("emitMeters reaches listeners", () => {
    const b = new FakeBackend();
    const cb = vi.fn();
    b.onMeters(cb);
    b.emitMeters({ ...ZERO_METERS, in: 0.5, out: 0.4, arpStep: 3 });
    expect(cb).toHaveBeenCalledWith({ ...ZERO_METERS, in: 0.5, out: 0.4, arpStep: 3 });
  });
});

// Stessa regola di Source/bridge/StateChannel.cpp::applyPreset: un parametro non
// menzionato dal preset torna al suo default di spec, uno esplicitamente a 0 resta 0.
describe("FakeBackend loadPreset", () => {
  it("un parametro non menzionato torna al default di spec; uno esplicito a 0 resta 0", async () => {
    const b = new FakeBackend();
    const index = PRESETS.findIndex((p) => p.name === "Sub Pulse");
    const preset = PRESETS[index]!;
    // Guardia: questo test si appoggia a queste due proprietà del preset "Sub Pulse".
    expect(preset.values.att).toBe(0);
    expect(preset.values.envVel).toBeUndefined();

    // Allontana entrambi i parametri dal loro default prima di caricare il preset.
    b.param("att").set(0.9);
    b.param("envVel").set(0.2);

    await b.loadPreset(index);

    // Valore esplicito 0 nel preset: deve restare 0, non ricadere sul default di spec (0.12).
    expect(b.param("att").get()).toBe(0);
    // Parametro non menzionato: torna al default di spec di "envVel" (0.6 in parameters.json).
    expect(b.param("envVel").get()).toBeCloseTo(Number(PARAM_SPECS.envVel.default));
  });

  it("un indice fuori range non tocca i parametri", async () => {
    const b = new FakeBackend();
    b.param("cutoff").set(0.1);
    await b.loadPreset(9999);
    expect(b.param("cutoff").get()).toBe(0.1);
  });
});

describe("mask delle note", () => {
  it("legge il bit giusto in ognuna delle quattro parole", () => {
    const frame = { ...ZERO_METERS, n0: 1 << 5, n1: 1 << 0, n2: 1 << 31, n3: 1 << 7 };
    const mask = noteMaskOf(frame);
    expect(isNoteActive(mask, 5)).toBe(true);
    expect(isNoteActive(mask, 32)).toBe(true);
    expect(isNoteActive(mask, 95)).toBe(true);
    expect(isNoteActive(mask, 103)).toBe(true);
    expect(isNoteActive(mask, 6)).toBe(false);
    expect(isNoteActive(mask, 127)).toBe(false);
  });

  it("un frame a zero non ha note accese", () => {
    const mask = noteMaskOf(ZERO_METERS);
    for (let n = 0; n < 128; n++) expect(isNoteActive(mask, n)).toBe(false);
  });
});
