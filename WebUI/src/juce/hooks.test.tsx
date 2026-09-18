import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";
import { act, renderHook } from "@testing-library/react";
import type { ReactNode } from "react";
import { FakeBackend } from "./fake-backend";
import { BridgeProvider } from "./provider";
import { useBoolParam, useBridgeState, useChoiceParam, useFloatParam, useIntParam, useMeters } from "./hooks";

const wrap = (b: FakeBackend) => ({ children }: { children: ReactNode }) => <BridgeProvider backend={b}>{children}</BridgeProvider>;

describe("param hooks", () => {
  it("useFloatParam reads, writes and re-renders on host change", () => {
    const b = new FakeBackend();
    const { result } = renderHook(() => useFloatParam("cutoff"), { wrapper: wrap(b) });
    expect(result.current.value).toBeCloseTo(0.62);
    expect(result.current.label).toBe("1.45 kHz");
    act(() => result.current.set(0.5));
    expect(b.param("cutoff").get()).toBe(0.5);
    act(() => b.push("cutoff", 0.25));
    expect(result.current.value).toBe(0.25);
  });
  it("useBoolParam / useChoiceParam / useIntParam convert", () => {
    const b = new FakeBackend();
    const w = wrap(b);
    const bool = renderHook(() => useBoolParam("filtOn"), { wrapper: w });
    expect(bool.result.current.checked).toBe(true);
    act(() => bool.result.current.set(false));
    expect(b.param("filtOn").get()).toBe(0);
    const ch = renderHook(() => useChoiceParam("ftype"), { wrapper: w });
    expect(ch.result.current.value).toBe("LP");
    act(() => ch.result.current.set("BP"));
    expect(b.param("ftype").get()).toBe(1);
    const n = renderHook(() => useIntParam("semi"), { wrapper: w });
    expect(n.result.current.value).toBe(0);
    act(() => n.result.current.set(7));
    expect(b.param("semi").get()).toBeCloseTo((7 + 12) / 24);
    expect(n.result.current.min).toBe(-12);
  });
});

describe("useBridgeState", () => {
  it("loads state, writes with an origin, ignores its own echo, applies external changes", async () => {
    const b = new FakeBackend({ state: { mods: [{ src: "lfo", target: "cutoff", depth: 0.25 }] } });
    const { result } = renderHook(() => useBridgeState(), { wrapper: wrap(b) });
    await act(async () => {});
    expect(result.current.mods).toHaveLength(1);
    await act(async () => { result.current.addMod("env", "res"); });
    expect(result.current.mods).toHaveLength(2);
    expect((await b.getState()).mods).toHaveLength(2);
    act(() => b.emitStateChanged({ version: 1, mods: [], arpSteps: new Array(16).fill(0) }, "host"));
    expect(result.current.mods).toHaveLength(0);
    await act(async () => { result.current.setDepth(0, 0.5); });   // no crash on empty
    await act(async () => { result.current.setArpSteps(new Array(16).fill(1)); });
    expect((await b.getState()).arpSteps[0]).toBe(1);
  });
});

describe("useMeters", () => {
  beforeEach(() => vi.useFakeTimers());
  afterEach(() => vi.useRealTimers());

  it("returns the last frame with peak hold, decaying by elapsed time", () => {
    const b = new FakeBackend();
    const { result } = renderHook(() => useMeters(), { wrapper: wrap(b) });
    act(() => b.emitMeters({ in: 0.8, out: 0.6, lfo: 0.1, arpStep: 2 }));
    expect(result.current.out).toBe(0.6);
    act(() => vi.advanceTimersByTime(1000 / 30));   // un tick a 30 Hz
    act(() => b.emitMeters({ in: 0, out: 0, lfo: 0, arpStep: 3 }));
    expect(result.current.out).toBeCloseTo(0.51);   // 0.6 · 0.85
    expect(result.current.arpStep).toBe(3);
  });

  it("applying the same frame twice at the same instant leaves the hold unchanged", () => {
    const b = new FakeBackend();
    const { result } = renderHook(() => useMeters(), { wrapper: wrap(b) });
    act(() => b.emitMeters({ in: 0.8, out: 0.6, lfo: 0.1, arpStep: 2 }));
    act(() => b.emitMeters({ in: 0, out: 0, lfo: 0, arpStep: 3 }));   // nessun tempo trascorso
    const afterFirst = result.current.out;
    act(() => b.emitMeters({ in: 0, out: 0, lfo: 0, arpStep: 3 }));   // stesso frame, stesso istante
    expect(result.current.out).toBe(afterFirst);
  });
});
