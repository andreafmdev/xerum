import { describe, expect, it, vi } from "vitest";
import { FakeBackend } from "./fake-backend";

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
    b.emitMeters({ in: 0.5, out: 0.4, lfo: 0, arpStep: 3 });
    expect(cb).toHaveBeenCalledWith({ in: 0.5, out: 0.4, lfo: 0, arpStep: 3 });
  });
});
