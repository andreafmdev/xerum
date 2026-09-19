// I livelli che alimentano gli anelli di modulazione disegnati attorno ai knob.
//
// Prima queste quattro sorgenti erano costanti scritte a mano dentro useSourceLevels: l'anello
// nasceva con la profondità giusta e non si muoveva mai, perché il livello non cambiava. Questi
// test dicono due cose sole, ma sono le due che mancavano: che i livelli vengono dal frame, e che
// un frame diverso sposta l'anello.

import { describe, expect, it } from "vitest";
import { act, renderHook } from "@testing-library/react";
import type { ReactNode } from "react";
import { FakeBackend } from "../../juce/fake-backend";
import { BridgeProvider } from "../../juce/provider";
import { ZERO_METERS } from "../../juce/backend";
import { MetersProvider, useSourceLevels } from "./MetersContext";
import { liveValue, MOD_SOURCES, type ModAssignment } from "../mod";

const wrap = (b: FakeBackend) => ({ children }: { children: ReactNode }) => (
  <BridgeProvider backend={b}>
    <MetersProvider>{children}</MetersProvider>
  </BridgeProvider>
);

describe("useSourceLevels", () => {
  it("riporta il frame dell'host, non delle costanti", () => {
    const b = new FakeBackend();
    const { result } = renderHook(() => useSourceLevels(), { wrapper: wrap(b) });

    act(() => b.emitMeters({ ...ZERO_METERS, lfo: -0.5, env: 0.9, env2: 0.4, vel: 0.25, mw: 0.1 }));

    expect(result.current).toEqual({ lfo: -0.5, env: 0.9, env2: 0.4, vel: 0.25, mw: 0.1 });
  });

  it("ogni sorgente del matrix ha un livello, e nessuna è inchiodata a un valore", () => {
    const b = new FakeBackend();
    const { result } = renderHook(() => useSourceLevels(), { wrapper: wrap(b) });

    act(() => b.emitMeters({ ...ZERO_METERS, lfo: 0.1, env: 0.2, env2: 0.3, vel: 0.4, mw: 0.5 }));
    const first = { ...result.current };
    act(() => b.emitMeters({ ...ZERO_METERS, lfo: 0.9, env: 0.8, env2: 0.7, vel: 0.6, mw: 0.5 }));

    for (const src of MOD_SOURCES) expect(typeof result.current[src]).toBe("number");
    // mw è volutamente lo stesso nei due frame: è l'unica che può non muoversi.
    for (const src of MOD_SOURCES.filter((s) => s !== "mw")) expect(result.current[src]).not.toBe(first[src]);
  });

  it("l'anello di una route env → cutoff si muove con l'inviluppo", () => {
    const b = new FakeBackend();
    const { result } = renderHook(() => useSourceLevels(), { wrapper: wrap(b) });
    const mods: ModAssignment[] = [{ src: "env", target: "cutoff", depth: 0.5 }];
    const base = 0.3;

    act(() => b.emitMeters({ ...ZERO_METERS, env: 0 }));
    const closed = liveValue(base, mods, result.current);

    act(() => b.emitMeters({ ...ZERO_METERS, env: 0.8 }));
    const open = liveValue(base, mods, result.current);

    expect(closed).toBeCloseTo(0.3);
    expect(open).toBeCloseTo(0.7);
    expect(open).toBeGreaterThan(closed);
  });
});
