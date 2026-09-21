// Contratto TypeScript condiviso dai due backend (JuceBackend e FakeBackend):
// astrae l'accesso ai parametri, allo stato di modulazione/arp e ai meter,
// così l'UI e gli hook non dipendono da JUCE né da come viene simulato.

import { PARAM_SPECS, type ParamId, type ParamSpec } from "../synth/params.generated";
import { fromIndex, fromInt } from "../synth/mapping";

/**
 * Sorgente di modulazione disponibile nel mod matrix.
 *
 * `env` e' l'inviluppo d'ampiezza riusato come modulatore; `env2` e' il secondo inviluppo, che
 * non governa nessun volume. Stessi nomi di engine::ModSource in Source/engine/ModMatrix.h:
 * sono le stringhe che viaggiano nel nodo MODS dello stato, quindi devono coincidere.
 */
export type ModSource = "lfo" | "env" | "env2" | "vel" | "mw";
/** Assegnazione di una sorgente a un parametro target con una profondità -1..1. */
export type ModAssignment = { src: ModSource; target: ParamId; depth: number };

/** Stato condiviso host↔WebView: versione, mod matrix e i 16 step dell'arp. */
export type BridgeState = { version: number; mods: ModAssignment[]; arpSteps: number[] };
/**
 * Frame di meter inviato a 30 Hz dall'host (o simulato dal FakeBackend).
 *
 * `lfo`, `env`, `env2`, `vel`, `mw` sono i livelli istantanei delle cinque sorgenti del mod
 * matrix — gli stessi nomi di ModSource — ed è ciò che fa muovere gli anelli attorno ai knob
 * modulati (liveValue() in ../synth/mod.ts). Prima l'host mandava il solo `lfo` e le altre
 * quattro erano costanti scritte nella UI: l'anello mostrava la profondità giusta e il movimento
 * sbagliato. Il contratto sta in Source/engine/MeterFrame.h e in Source/bridge/MeterChannel.cpp.
 *
 * `env`, `env2` e `vel` arrivano già come **picco dell'ultimo frame**, non come valore
 * istantaneo: a 30 Hz un attacco di pochi millisecondi passerebbe fra due frame. `lfo` è
 * bipolare −1..1 e `mw` è una posizione, quindi quei due sono istantanei.
 */
export type MeterFrame = {
  in: number;
  out: number;
  lfo: number;
  env: number;
  env2: number;
  vel: number;
  mw: number;
  arpStep: number;
  /**
   * Quali note stanno suonando, un bit per nota MIDI: `n0` copre 0..31, `n3` copre 96..127.
   *
   * Quattro parole da 32 bit e non due da 64 perché un uint64 non entra esatto nella mantissa di
   * un double, e il frame viaggia come JSON. Il contratto sta in Source/engine/MeterFrame.h e in
   * Source/bridge/MeterChannel.cpp.
   */
  n0: number;
  n1: number;
  n2: number;
  n3: number;
};

/** Frame a zero: valore iniziale di useMeters e base da cui i test costruiscono i loro frame. */
export const ZERO_METERS: MeterFrame = {
  in: 0, out: 0, lfo: 0, env: 0, env2: 0, vel: 0, mw: 0, arpStep: 0,
  n0: 0, n1: 0, n2: 0, n3: 0,
};

/** Un singolo parametro, come esposto all'UI: lettura/scrittura normalizzata 0..1 e gesture per l'automazione host. */
export interface ParamHandle {
  get(): number;                 // normalizzato 0..1 (bool → 0/1, int → (n-min)/(max-min), choice → i/(n-1))
  set(v: number): void;
  begin(): void; end(): void;    // gesture (no-op per bool/choice)
  subscribe(cb: () => void): () => void;
  readonly orphan: boolean;      // id sconosciuto al backend
}

/** Contratto implementato sia da JuceBackend (task 10) sia da FakeBackend. */
export interface Backend {
  readonly kind: "juce" | "fake";
  param(id: ParamId): ParamHandle;
  getState(): Promise<BridgeState>;
  setMods(mods: ModAssignment[], origin: string): Promise<void>;
  setArpSteps(steps: number[], origin: string): Promise<void>;
  loadPreset(index: number): Promise<void>;
  onStateChanged(cb: (s: BridgeState & { origin: string }) => void): () => void;
  onMeters(cb: (m: MeterFrame) => void): () => void;
  /** Note suonate dentro la UI. Finiscono nel MidiKeyboardState del processor: dal punto di vista
      del motore sono indistinguibili da quelle dell'host. `velocity` è 0..1. */
  noteOn(note: number, velocity: number): Promise<void>;
  noteOff(note: number): Promise<void>;
  /** Da chiamare su pointercancel, blur e smontaggio: senza, una nota può restare appesa. */
  allNotesOff(): Promise<void>;
  /** Posizione di una rotella, 0..1. `pitch` ha il centro a 0.5. */
  setWheel(kind: "pitch" | "mod", value: number): Promise<void>;
  /** Gli ingressi MIDI del sistema. `host` vero (VST3/AU): il MIDI arriva dall'host, lista vuota. */
  midiInputs(): Promise<MidiInputs>;
  /** Abilita o disabilita un ingresso (solo Standalone). */
  setMidiInputEnabled(id: string, enabled: boolean): Promise<void>;
  onMidiInputsChanged(cb: (m: MidiInputs) => void): () => void;
  /** Il device audio del sistema. `standalone` falso (VST3/AU): lo gestisce l'host. */
  audioSettings(): Promise<AudioSettings>;
  /** Le tre setter tornano "" se è andata, altrimenti il messaggio d'errore da mostrare. */
  setAudioOutput(id: string): Promise<string>;
  setSampleRate(hz: number): Promise<string>;
  setBufferSize(samples: number): Promise<string>;
  onAudioSettingsChanged(cb: (s: AudioSettings) => void): () => void;
  /**
   * Solo Standalone macOS. Da chiamare sul mousedown della fascia di trascinamento: prova il
   * trascinamento nativo a partire dall'evento corrente e torna vero se e' partito. Falso fuori
   * dallo Standalone, e anche dentro — perché WKWebView consegna i messaggi della bridge in modo
   * asincrono, l'evento corrente puo' non essere piu' il mousedown. In quel caso chi chiama deve
   * seguire da solo i mousemove e chiamare moveWindowBy: non è un ripiego per un bug, è la strada
   * presa su ogni versione di macOS dove l'evento non arriva in tempo.
   */
  beginWindowDrag(): Promise<boolean>;
  /** Il ripiego di beginWindowDrag quando non parte: sposta la finestra di (dx, dy) px di
      finestra. No-op fuori dallo Standalone. */
  moveWindowBy(dx: number, dy: number): Promise<void>;
  /** Quello che fa il doppio clic sulla barra del titolo nativa. No-op fuori dallo Standalone. */
  toggleWindowZoom(): Promise<void>;
  /** Quanto spazio lasciare libero per il semaforo, in px di finestra. 0 fuori dallo Standalone. */
  windowChrome(): Promise<{ trafficLightWidth: number }>;
}

export type MidiInputDevice = { id: string; name: string; enabled: boolean };
export type MidiInputs = { host: boolean; devices: MidiInputDevice[] };

export type AudioOutputDevice = { id: string; name: string };
/** Lo stato del device audio. `standalone` falso (VST3/AU): lo gestisce l'host, liste vuote. */
export type AudioSettings = {
  standalone: boolean;
  outputs: AudioOutputDevice[];
  currentOutput: string;
  sampleRates: number[];
  currentSampleRate: number;
  bufferSizes: number[];
  currentBufferSize: number;
  latencyMs: number;
};

/** Default normalizzato di uno spec (float: già 0..1; bool: 0/1; int: mappato; choice: indice mappato). */
export function defaultNormalised(spec: ParamSpec): number {
  switch (spec.kind) {
    case "bool": return spec.default ? 1 : 0;
    case "int": return fromInt(spec, Number(spec.default));
    case "choice": return fromIndex(spec, Number(spec.default));
    default: return Number(spec.default);
  }
}
export const specOf = (id: ParamId): ParamSpec => PARAM_SPECS[id];

/** Le quattro parole del mask, nell'ordine `n0..n3` di MeterFrame. */
export type NoteMask = readonly [number, number, number, number];

export const noteMaskOf = (m: MeterFrame): NoteMask => [m.n0, m.n1, m.n2, m.n3];

/** Il bit della nota. Gli operatori bit a bit di JS lavorano su int32: una parola oltre 2^31
    arriva qui come numero positivo grande e viene riconvertita a int32 dall'`&`, quindi il
    confronto resta corretto anche per il bit più alto. */
export const isNoteActive = (mask: NoteMask, note: number): boolean =>
  ((mask[note >> 5] ?? 0) & (1 << (note & 31))) !== 0;
