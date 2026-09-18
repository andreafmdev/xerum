// Hook che collegano l'UI React al Backend: un ParamHandle per ogni parametro
// (float/bool/choice/int), lo stato condiviso della mod matrix/arp e i meter.

import { useCallback, useEffect, useMemo, useRef, useState, useSyncExternalStore } from "react";
import { PARAM_SPECS, type ParamId } from "../synth/params.generated";
import { formatValue, fromIndex, fromInt, paramLabel, toIndex, toInt } from "../synth/mapping";
import type { BridgeState, MeterFrame, ModAssignment, ModSource } from "./backend";
import { useBackend } from "./provider";

function useHandle(id: ParamId) {
  const h = useBackend().param(id);
  // Bind memoizzati per handle: senza useMemo, subscribe/get sarebbero nuove funzioni
  // ad ogni render e useSyncExternalStore si ri-sottoscriverebbe ad ogni commit.
  const store = useMemo(() => ({ subscribe: (cb: () => void) => h.subscribe(cb), get: () => h.get() }), [h]);
  const value = useSyncExternalStore(store.subscribe, store.get);
  return { h, value };
}

export function useFloatParam(id: ParamId) {
  const { h, value } = useHandle(id);
  const spec = PARAM_SPECS[id];
  return { value, set: (v: number) => h.set(v), begin: () => h.begin(), end: () => h.end(), spec, label: formatValue(spec, value), name: paramLabel(spec) };
}
export function useBoolParam(id: ParamId) {
  const { h, value } = useHandle(id);
  return { checked: value >= 0.5, set: (b: boolean) => h.set(b ? 1 : 0) };
}
export function useChoiceParam(id: ParamId) {
  const { h, value } = useHandle(id);
  const spec = PARAM_SPECS[id];
  const options = spec.options ?? [];
  return {
    value: options[toIndex(spec, value)]?.value ?? "",
    set: (v: string) => {
      const i = options.findIndex((o) => o.value === v);
      if (i >= 0) h.set(fromIndex(spec, i));
    },
    options,
  };
}
export function useIntParam(id: ParamId) {
  const { h, value } = useHandle(id);
  const spec = PARAM_SPECS[id];
  return { value: toInt(spec, value), set: (n: number) => h.set(fromInt(spec, n)), min: spec.map?.min ?? 0, max: spec.map?.max ?? 0 };
}

export const DEFAULT_DEPTH = 0.3;
/** Aggiunge un'assegnazione; identità invariata se la coppia src/target esiste già. */
export function addModPure(mods: ModAssignment[], src: ModSource, target: ParamId): ModAssignment[] {
  return mods.some((m) => m.src === src && m.target === target) ? mods : [...mods, { src, target, depth: DEFAULT_DEPTH }];
}

const EMPTY: BridgeState = { version: 1, mods: [], arpSteps: new Array(16).fill(0) };

export function useBridgeState() {
  const backend = useBackend();
  // Origin locale: distingue le scritture di questa istanza dagli echo dell'host,
  // così un evento stateChanged con lo stesso origin viene ignorato.
  const origin = useRef(Math.random().toString(36).slice(2));
  const [state, setState] = useState<BridgeState>(EMPTY);
  // Rispecchia state.mods per addMod/setDepth/removeMod: aggiornato in modo sincrono
  // ad ogni scrittura, così due chiamate ravvicinate non leggono un valore stale
  // prima che React applichi il re-render (niente closure sul render precedente).
  const modsRef = useRef(state.mods);

  useEffect(() => {
    let alive = true;
    backend.getState().then(
      (s) => { if (alive) { modsRef.current = s.mods; setState(s); } },
      (e) => console.warn("[bridge] getState fallita", e),
    );
    // Idempotente: applicare due volte lo stesso evento produce lo stesso stato
    // (contro il backend JUCE vero gli unsubscribe sono no-op, i listener possono
    // duplicarsi dopo un remount — setState con un valore equivalente non cambia nulla).
    const off = backend.onStateChanged((s) => {
      if (s.origin !== origin.current) { modsRef.current = s.mods; setState({ version: s.version, mods: s.mods, arpSteps: s.arpSteps }); }
    });
    return () => { alive = false; off(); };
  }, [backend]);

  const setMods = useCallback(
    (mods: ModAssignment[]) => { modsRef.current = mods; setState((s) => ({ ...s, mods })); void backend.setMods(mods, origin.current); },
    [backend],
  );
  const setArpSteps = useCallback(
    (arpSteps: number[]) => { setState((s) => ({ ...s, arpSteps })); void backend.setArpSteps(arpSteps, origin.current); },
    [backend],
  );

  return {
    mods: state.mods,
    arpSteps: state.arpSteps,
    setMods,
    setArpSteps,
    addMod: (src: ModSource, target: ParamId) => setMods(addModPure(modsRef.current, src, target)),
    setDepth: (i: number, depth: number) => setMods(modsRef.current.map((m, j) => (j === i ? { ...m, depth } : m))),
    removeMod: (i: number) => setMods(modsRef.current.filter((_, j) => j !== i)),
  };
}

export function useMeters(): MeterFrame {
  const backend = useBackend();
  const [frame, setFrame] = useState<MeterFrame>({ in: 0, out: 0, lfo: 0, arpStep: 0 });
  // Istante dell'ultimo frame applicato: il decay dipende dal tempo trascorso (in
  // "tick" da 1000/30 ms), non dal numero di eventi ricevuti. Così due copie dello
  // stesso frame consegnate nello stesso istante (gli unsubscribe sono no-op contro
  // il backend JUCE vero, i listener possono duplicarsi dopo un remount) applicano
  // lo stesso decay (≈1, tempo trascorso ≈0) invece di comprimere 0.85 due volte.
  const at = useRef<number | null>(null);
  useEffect(
    () =>
      backend.onMeters((m) => {
        const now = Date.now();
        const elapsedTicks = at.current === null ? Infinity : (now - at.current) / (1000 / 30);
        const decay = Math.min(1, Math.pow(0.85, elapsedTicks));
        at.current = now;
        setFrame((p) => ({ in: Math.max(m.in, p.in * decay), out: Math.max(m.out, p.out * decay), lfo: m.lfo, arpStep: m.arpStep }));
      }),
    [backend],
  );
  return frame;
}
