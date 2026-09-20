// Lo store dei meter: un solo listener sul backend per tutta la UI, e un frame letto con un
// selettore da useSyncExternalStore. Un componente che seleziona `f.lfo` ri-renderizza solo
// quando cambia l'LFO, non a ogni frame; l'arp playhead di uno step solo quando quello step si
// accende o si spegne. Prima il frame intero viveva in uno useState dentro un provider, e ogni
// consumatore ri-renderizzava 30 volte al secondo qualunque cosa leggesse.

import { ZERO_METERS, type Backend, type MeterFrame } from "./backend";

export type MeterStore = {
  subscribe(cb: () => void): () => void;
  get(): MeterFrame;
};

/** Il numero, o zero: scarta undefined, null e NaN in arrivo dal ponte. */
const finite = (v: number) => (typeof v === "number" && Number.isFinite(v) ? v : 0);

const stores = new WeakMap<Backend, MeterStore>();

/** Lo store del backend, creato alla prima richiesta. Si attacca a `onMeters` con il primo
    abbonato e si stacca con l'ultimo. */
export function meterStore(backend: Backend): MeterStore {
  const cached = stores.get(backend);
  if (cached) return cached;

  let frame: MeterFrame = ZERO_METERS;
  // Istante dell'ultimo frame applicato: il decay dipende dal tempo trascorso (in "tick" da
  // 1000/30 ms), non dal numero di eventi ricevuti. Così due copie dello stesso frame
  // consegnate nello stesso istante (un listener doppione dopo un remount) applicano lo stesso
  // decay (≈1, tempo trascorso ≈0) invece di comprimere 0.85 due volte.
  let at: number | null = null;
  const subs = new Set<() => void>();
  let off: (() => void) | null = null;

  const apply = (m: MeterFrame) => {
    const now = Date.now();
    const elapsedTicks = at === null ? Infinity : (now - at) / (1000 / 30);
    const decay = Math.min(1, Math.pow(0.85, elapsedTicks));
    at = now;
    const p = frame;
    // Il peak hold con decay è solo per i due meter audio, dove serve a rendere leggibile un
    // picco che dura un frame. Le cinque sorgenti passano intatte: env/env2/vel sono già il
    // picco del frame (lo prende il motore, vedi Source/engine/MeterFrame.h), e tenerle appese
    // per altri 100 ms farebbe scendere l'anello molto dopo l'inviluppo che mostra.
    //
    // finite() su ogni campo per la stessa ragione di OrphanHandle in juce-backend.ts: un
    // binario più vecchio della WebUI manda un frame senza env/env2/vel/mw, e quegli undefined
    // finirebbero dentro liveValue() — un NaN, cioè un anello che sparisce invece di uno fermo.
    frame = {
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
    };
    for (const cb of subs) cb();
  };

  const store: MeterStore = {
    subscribe(cb) {
      subs.add(cb);
      if (!off) off = backend.onMeters(apply);
      return () => {
        subs.delete(cb);
        if (subs.size === 0 && off) {
          off();
          off = null;
        }
      };
    },
    get: () => frame,
  };
  stores.set(backend, store);
  return store;
}
