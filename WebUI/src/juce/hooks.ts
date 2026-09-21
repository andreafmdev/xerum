// Hook che collegano l'UI React al Backend: un ParamHandle per ogni parametro
// (float/bool/choice/int), lo stato condiviso della mod matrix/arp e i meter.

import { useCallback, useEffect, useMemo, useRef, useState, useSyncExternalStore } from "react";
import { PARAM_SPECS, type ParamId } from "../synth/params.generated";
import { formatValue, fromIndex, fromInt, paramLabel, toIndex, toInt } from "../synth/mapping";
import type { AudioSettings, BridgeState, MeterFrame, MidiInputs, ModAssignment, ModSource } from "./backend";
import { meterStore } from "./meters";
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

const DEFAULT_DEPTH = 0.3;
/** Aggiunge un'assegnazione; identità invariata se la coppia src/target esiste già. */
export function addModPure(mods: ModAssignment[], src: ModSource, target: ParamId): ModAssignment[] {
  return mods.some((m) => m.src === src && m.target === target) ? mods : [...mods, { src, target, depth: DEFAULT_DEPTH }];
}

const ARP_STEPS = 16;
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

/**
 * Gli ingressi MIDI del sistema e il modo di sceglierli. `host` vero (VST3/AU) finche' il backend
 * non risponde e nel plugin: la barra mostra "HOST". Nello Standalone la lista arriva da
 * getMidiInputs e si aggiorna con l'evento midiInputsChanged.
 */
export function useMidiInputs() {
  const backend = useBackend();
  const [inputs, setInputs] = useState<MidiInputs>({ host: true, devices: [] });
  useEffect(() => {
    let alive = true;
    backend.midiInputs().then((m) => { if (alive) setInputs(m); }, (e) => console.warn("[bridge] getMidiInputs fallita", e));
    const off = backend.onMidiInputsChanged((m) => setInputs(m));
    return () => { alive = false; off(); };
  }, [backend]);
  /** "all" abilita ogni ingresso; un id abilita quello e spegne gli altri. */
  const select = useCallback(async (choice: string) => {
    const devices = inputs.devices;
    if (choice === "all") {
      for (const d of devices) await backend.setMidiInputEnabled(d.id, true);
      return;
    }
    await backend.setMidiInputEnabled(choice, true);
    for (const d of devices) if (d.id !== choice) await backend.setMidiInputEnabled(d.id, false);
  }, [backend, inputs.devices]);
  return { inputs, select };
}

/**
 * Le impostazioni del device audio e il modo di cambiarle. `standalone` falso finche' il
 * backend non risponde e nel plugin: il pannello mostra la riga "li gestisce l'host".
 *
 * `error` tiene il messaggio che la setter ha restituito: un device che non si apre — sample
 * rate non supportato, scheda staccata — deve dirlo, non restare in silenzio con la vecchia
 * impostazione ancora attiva. Si azzera al cambio riuscito successivo.
 */
export function useAudioSettings() {
  const backend = useBackend();
  const [settings, setSettings] = useState<AudioSettings>({
    standalone: false, outputs: [], currentOutput: "",
    sampleRates: [], currentSampleRate: 0, bufferSizes: [], currentBufferSize: 0, latencyMs: 0,
  });
  const [error, setError] = useState("");
  useEffect(() => {
    let alive = true;
    backend.audioSettings().then((s) => { if (alive) setSettings(s); },
      (e) => console.warn("[bridge] getAudioSettings fallita", e));
    const off = backend.onAudioSettingsChanged((s) => setSettings(s));
    return () => { alive = false; off(); };
  }, [backend]);

  // Senza catch, un rifiuto della promise (setOutput/setSampleRate/setBufferSize) sarebbe un
  // unhandled rejection: il fallimento "normale" gia' passa da qui come stringa risolta
  // (l'errore restituito da applyAudio), quindi un reject e' sempre un guasto del bridge stesso.
  const run = useCallback(async (p: Promise<string>) => {
    try { setError(await p); }
    catch (e) { console.warn("[bridge] cambio impostazioni audio fallito", e); }
  }, []);
  return {
    settings,
    error,
    setOutput: useCallback((id: string) => run(backend.setAudioOutput(id)), [backend, run]),
    setSampleRate: useCallback((hz: number) => run(backend.setSampleRate(hz)), [backend, run]),
    setBufferSize: useCallback((n: number) => run(backend.setBufferSize(n)), [backend, run]),
  };
}

/**
 * Un valore scelto dal frame dei meter. Il componente ri-renderizza solo quando il risultato
 * del selettore cambia (confronto Object.is), quindi il selettore deve tornare un primitivo o
 * il frame stesso: un oggetto nuovo a ogni chiamata farebbe ri-renderizzare sempre.
 */
export function useMeterValue<T>(select: (f: MeterFrame) => T): T {
  const store = meterStore(useBackend());
  return useSyncExternalStore(store.subscribe, () => select(store.get()));
}

/** Il frame intero: ri-renderizza a ogni frame. Per un solo campo usare useMeterValue. */
export function useMeters(): MeterFrame {
  return useMeterValue((f) => f);
}
