import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";
import { act, renderHook, waitFor } from "@testing-library/react";
import type { ReactNode } from "react";
import { FakeBackend } from "./fake-backend";
import { BridgeProvider } from "./provider";
import { parseState, useAudioSettings, useBoolParam, useBridgeState, useChoiceParam, useFloatParam, useIntParam, useMeters } from "./hooks";
import { ZERO_METERS, type BridgeState } from "./backend";

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

describe("parseState", () => {
  const good: BridgeState = { version: 1, mods: [{ src: "lfo", target: "cutoff", depth: 0.2 }], arpSteps: new Array(16).fill(0) };
  it("accepts a well-formed payload", () => expect(parseState(good)).toEqual(good));
  it("rejects unknown versions, malformed mods, wrong step counts and non-objects", () => {
    expect(parseState({ ...good, version: 2 })).toBeNull();
    expect(parseState({ ...good, mods: [{ src: "lfo" }] })).toBeNull();
    expect(parseState({ ...good, mods: [{ src: "lfo", target: "cutoff", depth: "deep" }] })).toBeNull();
    expect(parseState({ ...good, mods: "nope" })).toBeNull();
    expect(parseState({ ...good, arpSteps: new Array(8).fill(0) })).toBeNull();
    expect(parseState({ ...good, arpSteps: new Array(16).fill("x") })).toBeNull();
    expect(parseState(null)).toBeNull();
    expect(parseState("nope")).toBeNull();
  });
});

describe("useBridgeState payload validation", () => {
  const bad = (p: unknown) => p as BridgeState;

  it("warns and keeps the current state on a malformed payload, applies a valid one", async () => {
    const warn = vi.spyOn(console, "warn").mockImplementation(() => {});
    const b = new FakeBackend({ state: { mods: [{ src: "lfo", target: "cutoff", depth: 0.25 }] } });
    const { result } = renderHook(() => useBridgeState(), { wrapper: wrap(b) });
    await act(async () => {});
    expect(result.current.mods).toHaveLength(1);

    act(() => b.emitStateChanged(bad({ version: 1, mods: [{ src: "lfo" }], arpSteps: new Array(16).fill(0) }), "host"));
    expect(warn).toHaveBeenCalledWith("[bridge] stato non valido", expect.anything());
    expect(result.current.mods).toHaveLength(1);

    act(() => b.emitStateChanged(bad({ version: 2, mods: [], arpSteps: new Array(16).fill(0) }), "host"));
    expect(warn).toHaveBeenCalledTimes(2);
    expect(result.current.mods).toHaveLength(1);

    act(() => b.emitStateChanged({ version: 1, mods: [], arpSteps: new Array(16).fill(0) }, "host"));
    expect(result.current.mods).toHaveLength(0);
    expect(warn).toHaveBeenCalledTimes(2);
    warn.mockRestore();
  });

  it("warns and keeps the empty state when getState resolves with a malformed payload", async () => {
    const warn = vi.spyOn(console, "warn").mockImplementation(() => {});
    const b = new FakeBackend();
    vi.spyOn(b, "getState").mockResolvedValue(bad({ version: 1, arpSteps: new Array(16).fill(0) }));
    const { result } = renderHook(() => useBridgeState(), { wrapper: wrap(b) });
    await act(async () => {});
    expect(warn).toHaveBeenCalledWith("[bridge] stato non valido", expect.anything());
    expect(result.current.mods).toHaveLength(0);
    expect(result.current.arpSteps).toHaveLength(16);
    warn.mockRestore();
  });
});

describe("useMeters", () => {
  beforeEach(() => vi.useFakeTimers());
  afterEach(() => vi.useRealTimers());

  it("returns the last frame with peak hold, decaying by elapsed time", () => {
    const b = new FakeBackend();
    const { result } = renderHook(() => useMeters(), { wrapper: wrap(b) });
    act(() => b.emitMeters({ ...ZERO_METERS, in: 0.8, out: 0.6, lfo: 0.1, arpStep: 2 }));
    expect(result.current.out).toBe(0.6);
    act(() => vi.advanceTimersByTime(1000 / 30));   // un tick a 30 Hz
    act(() => b.emitMeters({ ...ZERO_METERS, arpStep: 3 }));
    expect(result.current.out).toBeCloseTo(0.51);   // 0.6 · 0.85
    expect(result.current.arpStep).toBe(3);
  });

  it("un frame senza le sorgenti (binario più vecchio della UI) non produce NaN", () => {
    const b = new FakeBackend();
    const { result } = renderHook(() => useMeters(), { wrapper: wrap(b) });
    // Quello che mandava MeterChannel prima che le cinque sorgenti esistessero.
    act(() => b.emitMeters({ in: 0.5, out: 0.5, lfo: 0.2, arpStep: 1 } as never));
    for (const v of Object.values(result.current)) expect(Number.isFinite(v)).toBe(true);
    expect(result.current.env).toBe(0);
  });

  it("applying the same frame twice at the same instant leaves the hold unchanged", () => {
    const b = new FakeBackend();
    const { result } = renderHook(() => useMeters(), { wrapper: wrap(b) });
    act(() => b.emitMeters({ ...ZERO_METERS, in: 0.8, out: 0.6, lfo: 0.1, arpStep: 2 }));
    act(() => b.emitMeters({ ...ZERO_METERS, arpStep: 3 }));   // nessun tempo trascorso
    const afterFirst = result.current.out;
    act(() => b.emitMeters({ ...ZERO_METERS, arpStep: 3 }));   // stesso frame, stesso istante
    expect(result.current.out).toBe(afterFirst);
  });
});

describe("useAudioSettings", () => {
  it("parte da standalone falso e prende la fotografia dal backend", async () => {
    const backend = new FakeBackend({
      audioSettings: {
        standalone: true,
        outputs: [{ id: "Scarlett", name: "Focusrite 2i2" }],
        currentOutput: "Scarlett",
        sampleRates: [44100, 48000], currentSampleRate: 48000,
        bufferSizes: [64, 128], currentBufferSize: 128,
        latencyMs: 2.6666666666666665,
      },
    });
    const { result } = renderHook(() => useAudioSettings(), { wrapper: wrap(backend) });
    await waitFor(() => expect(result.current.settings.standalone).toBe(true));
    expect(result.current.settings.outputs[0]!.name).toBe("Focusrite 2i2");
  });

  it("tiene l'errore che la setter restituisce invece di ingoiarlo", async () => {
    const backend = new FakeBackend({ audioSettings: { standalone: true, outputs: [], currentOutput: "",
      sampleRates: [], currentSampleRate: 0, bufferSizes: [], currentBufferSize: 0, latencyMs: 0 } });
    backend.failNextAudioChange("Il device non si apre a 96 kHz");
    const { result } = renderHook(() => useAudioSettings(), { wrapper: wrap(backend) });
    await act(async () => { await result.current.setSampleRate(96000); });
    expect(result.current.error).toBe("Il device non si apre a 96 kHz");
  });

  it("un cambio dal sistema aggiorna la fotografia", async () => {
    const backend = new FakeBackend({ audioSettings: { standalone: true, outputs: [], currentOutput: "",
      sampleRates: [], currentSampleRate: 0, bufferSizes: [], currentBufferSize: 0, latencyMs: 0 } });
    const { result } = renderHook(() => useAudioSettings(), { wrapper: wrap(backend) });
    await waitFor(() => expect(result.current.settings.standalone).toBe(true));
    act(() => backend.emitAudioSettingsChanged({ standalone: true, outputs: [], currentOutput: "MacBook",
      sampleRates: [], currentSampleRate: 0, bufferSizes: [], currentBufferSize: 0, latencyMs: 0 }));
    await waitFor(() => expect(result.current.settings.currentOutput).toBe("MacBook"));
  });
});
