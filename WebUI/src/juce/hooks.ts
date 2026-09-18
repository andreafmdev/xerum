// Hook che collegano l'UI React al Backend: un ParamHandle per ogni parametro
// (float/bool/choice/int), lo stato condiviso della mod matrix/arp e i meter.

import { useCallback, useEffect, useRef, useState, useSyncExternalStore } from "react";
import { PARAM_SPECS, type ParamId } from "../synth/params.generated";
import { formatValue, fromIndex, fromInt, paramLabel, toIndex, toInt } from "../synth/mapping";
import type { BridgeState, MeterFrame, ModAssignment, ModSource } from "./backend";
import { useBackend } from "./provider";

function useHandle(id: ParamId) {
  const h = useBackend().param(id);
  const value = useSyncExternalStore(h.subscribe.bind(h), h.get.bind(h));
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
  useEffect(
    // Idempotente: riapplicare lo stesso frame due volte (listener duplicati dopo
    // un remount, bug noto del backend JUCE) lascia il peak hold invariato.
    () => backend.onMeters((m) => setFrame((p) => ({ in: Math.max(m.in, p.in * 0.85), out: Math.max(m.out, p.out * 0.85), lfo: m.lfo, arpStep: m.arpStep }))),
    [backend],
  );
  return frame;
}
