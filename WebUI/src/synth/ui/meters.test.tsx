// I livelli che alimentano gli anelli di modulazione disegnati attorno ai knob.
//
// Prima queste sorgenti erano costanti scritte a mano: l'anello nasceva con la profondità giusta
// e non si muoveva mai. Questi test dicono che il valore live viene dal frame dell'host, che un
// frame diverso sposta l'anello, e che un frame che non tocca il risultato non ri-renderizza.

import { describe, expect, it } from "vitest";
import { act, renderHook } from "@testing-library/react";
import type { ReactNode } from "react";
import { FakeBackend } from "../../juce/fake-backend";
import { useMeterValue } from "../../juce/hooks";
import { BridgeProvider } from "../../juce/provider";
import { ZERO_METERS } from "../../juce/backend";
import { useLiveValue } from "./meters";
import { MOD_SOURCES, type ModAssignment } from "../mod";

const wrap = (b: FakeBackend) => ({ children }: { children: ReactNode }) => <BridgeProvider backend={b}>{children}</BridgeProvider>;

describe("useLiveValue", () => {
  it("l'anello di una route env → cutoff si muove con l'inviluppo", () => {
    const b = new FakeBackend();
    const mods: ModAssignment[] = [{ src: "env", target: "cutoff", depth: 0.5 }];
    const { result } = renderHook(() => useLiveValue(0.3, mods), { wrapper: wrap(b) });

    act(() => b.emitMeters({ ...ZERO_METERS, env: 0 }));
    const closed = result.current;
    act(() => b.emitMeters({ ...ZERO_METERS, env: 0.8 }));
    const open = result.current;

    expect(closed).toBeCloseTo(0.3);
    expect(open).toBeCloseTo(0.7);
  });

  it("ogni sorgente del matrix sposta il valore, nessuna è inchiodata", () => {
    const b = new FakeBackend();
    for (const src of MOD_SOURCES) {
      const mods: ModAssignment[] = [{ src, target: "cutoff", depth: 0.5 }];
      const { result, unmount } = renderHook(() => useLiveValue(0.2, mods), { wrapper: wrap(b) });
      act(() => b.emitMeters({ ...ZERO_METERS, [src]: 0 }));
      const low = result.current;
      act(() => b.emitMeters({ ...ZERO_METERS, [src]: 0.6 }));
      expect(result.current).toBeGreaterThan(low);
      unmount();
    }
  });

  it("un frame che muove solo altre sorgenti non ri-renderizza", () => {
    const b = new FakeBackend();
    const mods: ModAssignment[] = [{ src: "env", target: "cutoff", depth: 0.5 }];
    let renders = 0;
    renderHook(() => { renders++; return useLiveValue(0.3, mods); }, { wrapper: wrap(b) });
    const before = renders;
    act(() => b.emitMeters({ ...ZERO_METERS, lfo: 0.9, vel: 0.4 }));
    expect(renders).toBe(before);
  });
});

describe("useMeterValue", () => {
  it("ri-renderizza solo quando cambia il valore selezionato", () => {
    const b = new FakeBackend();
    let renders = 0;
    const { result } = renderHook(() => { renders++; return useMeterValue((f) => f.arpStep === 3); }, { wrapper: wrap(b) });
    expect(result.current).toBe(false);
    const before = renders;
    act(() => b.emitMeters({ ...ZERO_METERS, arpStep: 1 }));
    act(() => b.emitMeters({ ...ZERO_METERS, arpStep: 2, lfo: 0.5 }));
    expect(renders).toBe(before);
    act(() => b.emitMeters({ ...ZERO_METERS, arpStep: 3 }));
    expect(result.current).toBe(true);
    expect(renders).toBe(before + 1);
  });

  it("un solo listener sul backend per quanti siano i consumatori, staccato con l'ultimo", () => {
    const b = new FakeBackend();
    const a = renderHook(() => useMeterValue((f) => f.lfo), { wrapper: wrap(b) });
    const c = renderHook(() => useMeterValue((f) => f.env), { wrapper: wrap(b) });
    expect(b.meterListeners()).toBe(1);
    a.unmount();
    expect(b.meterListeners()).toBe(1);
    c.unmount();
    expect(b.meterListeners()).toBe(0);
  });
});
