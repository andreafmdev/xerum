// Hook che collegano l'UI React al Backend: un ParamHandle per ogni parametro
// (float/bool/choice/int), lo stato condiviso della mod matrix/arp e i meter.

import { useCallback, useEffect, useMemo, useRef, useState, useSyncExternalStore } from "react";
import { PARAM_SPECS, type ParamId } from "../synth/params.generated";
import { formatValue, fromIndex, fromInt, paramLabel, toIndex, toInt } from "../synth/mapping";
import { ZERO_METERS, type BridgeState, type MeterFrame, type ModAssignment, type ModSource } from "./backend";
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

export const ARP_STEPS = 16;
const EMPTY: BridgeState = { version: 1, mods: [], arpSteps: new Array(ARP_STEPS).fill(0) };

const isMod = (m: unknown): m is ModAssignment => {
  const o = m as Record<string, unknown> | null;
  return typeof o === "object" && o !== null && typeof o.src === "string" && typeof o.target === "string" && typeof o.depth === "number";
};

/** Valida un payload getState/stateChanged (spec §8/§9): null se non conforme.
    Il bridge è un confine di fiducia: un binario più vecchio o un payload rotto
    non deve poter inserire mods/step malformati nello stato dell'UI. */
export function parseState(raw: unknown): BridgeState | null {
  const o = raw as Record<string, unknown> | null;
  if (typeof o !== "object" || o === null) return null;
  if (o.version !== 1) return null;
  if (!Array.isArray(o.mods) || !o.mods.every(isMod)) return null;
  if (!Array.isArray(o.arpSteps) || o.arpSteps.length !== ARP_STEPS || !o.arpSteps.every((n) => typeof n === "number")) return null;
  return { version: 1, mods: o.mods, arpSteps: o.arpSteps };
}

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
    // Un payload non conforme non entra mai nello stato: si logga e si tiene quello corrente.
    const apply = (raw: unknown) => {
      const s = parseState(raw);
      if (s === null) { console.warn("[bridge] stato non valido", raw); return; }
      modsRef.current = s.mods;
      setState(s);
    };
    backend.getState().then(
      (raw) => { if (alive) apply(raw); },
      (e) => console.warn("[bridge] getState fallita", e),
    );
    // Idempotente: applicare due volte lo stesso evento produce lo stesso stato,
    // quindi un eventuale doppione di un listener non cambia nulla.
    const off = backend.onStateChanged((s) => {
      if (s?.origin !== origin.current) apply(s);
    });
    return () => { alive = false; off(); };
  }, [backend]);

  const setMods = useCallback(
    (mods: ModAssignment[]) => { modsRef.current = mods; setState((s) => ({ ...s, mods })); void backend.setMods(mods, origin.current).catch((e) => console.warn("[bridge] scrittura fallita", e)); },
    [backend],
  );
  const setArpSteps = useCallback(
    (arpSteps: number[]) => { setState((s) => ({ ...s, arpSteps })); void backend.setArpSteps(arpSteps, origin.current).catch((e) => console.warn("[bridge] scrittura fallita", e)); },
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

/** Il numero, o zero: scarta undefined, null e NaN in arrivo dal ponte. */
const finite = (v: number) => (typeof v === "number" && Number.isFinite(v) ? v : 0);

export function useMeters(): MeterFrame {
  const backend = useBackend();
  const [frame, setFrame] = useState<MeterFrame>(ZERO_METERS);
  // Istante dell'ultimo frame applicato: il decay dipende dal tempo trascorso (in
  // "tick" da 1000/30 ms), non dal numero di eventi ricevuti. Così due copie dello
  // stesso frame consegnate nello stesso istante (un listener doppione dopo un
  // remount) applicano lo stesso decay (≈1, tempo trascorso ≈0) invece di
  // comprimere 0.85 due volte.
  const at = useRef<number | null>(null);
  useEffect(
    () =>
      backend.onMeters((m) => {
        const now = Date.now();
        const elapsedTicks = at.current === null ? Infinity : (now - at.current) / (1000 / 30);
        const decay = Math.min(1, Math.pow(0.85, elapsedTicks));
        at.current = now;
        // Il peak hold con decay è solo per i due meter audio, dove serve a rendere leggibile
        // un picco che dura un frame. Le cinque sorgenti passano intatte: env/env2/vel sono già
        // il picco del frame (lo prende il motore, vedi Source/engine/MeterFrame.h), e tenerle
        // appese per altri 100 ms farebbe scendere l'anello molto dopo l'inviluppo che mostra —
        // di nuovo un anello che non dice la verità, solo con un ritardo invece che con una
        // costante.
        //
        // finite() su ogni campo per la stessa ragione di OrphanHandle in juce-backend.ts: un
        // binario più vecchio della WebUI manda un frame senza env/env2/vel/mw, e quegli
        // undefined finirebbero dentro liveValue() — un NaN, cioè un anello che sparisce invece
        // di un anello fermo.
        setFrame((p) => ({
          in: Math.max(finite(m.in), p.in * decay),
          out: Math.max(finite(m.out), p.out * decay),
          lfo: finite(m.lfo),
          env: finite(m.env),
          env2: finite(m.env2),
          vel: finite(m.vel),
          mw: finite(m.mw),
          arpStep: finite(m.arpStep),
          // Il mask, come lfo/mw, e' uno stato istantaneo: passa intatto, nessun decay.
          n0: finite(m.n0),
          n1: finite(m.n1),
          n2: finite(m.n2),
          n3: finite(m.n3),
        }));
      }),
    [backend],
  );
  return frame;
}
