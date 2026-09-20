// Implementazione di Backend sopra i relay JUCE 8: ogni parametro del plugin ha
// un relay registrato lato C++ con lo stesso id (slider per float/int, toggle
// per bool, combo per choice). Qui li avvolgiamo in ParamHandle normalizzati
// 0..1 e traduciamo getState/setMods/setArpSteps in native function call.

import { PARAM_SPECS, type ParamId } from "../synth/params.generated";
import { fromIndex, toIndex } from "../synth/mapping";
import { defaultNormalised, type Backend, type BridgeState, type MeterFrame, type ModAssignment, type ParamHandle } from "./backend";
import type * as Juce from "./vendor/juce-frontend/index";

declare global {
  interface Window {
    __JUCE__?: {
      backend: { addEventListener(id: string, fn: (p: any) => void): number; removeEventListener(id: number): void; emitEvent(id: string, p: unknown): void };
      initialisationData: { __juce__sliders: string[]; __juce__toggles: string[]; __juce__comboBoxes: string[]; __juce__functions: string[] };
    };
  }
}

/** True solo dentro la WebView del plugin: fuori (browser, test) manca window.__JUCE__. */
export const hasJuce = () => typeof window !== "undefined" && !!window.__JUCE__?.backend;

type Subs = Set<() => void>;
const subscribeTo = (list: Juce.ListenerList, subs: Subs) => { list.addListener(() => { for (const s of subs) s(); }); };

// Durante una gesture l'handle risponde con il valore impostato localmente e
// ignora gli echo dell'host, cosi' il knob non "salta" mentre lo si trascina.
class SliderHandle implements ParamHandle {
  readonly orphan = false;
  private subs: Subs = new Set();
  private dragging = false;
  private local = 0;
  constructor(private st: Juce.SliderState) { subscribeTo(st.valueChangedEvent, this.subs); }
  get() { return this.dragging ? this.local : this.st.getNormalisedValue(); }
  // setNormalisedValue non fa scattare i listener del relay: notifichiamo noi,
  // altrimenti il knob si ridisegnerebbe solo all'eco del C++ (un giro di ritardo).
  set(v: number) { this.local = v; this.st.setNormalisedValue(v); this.notify(); }
  private notify() { for (const s of this.subs) s(); }
  begin() { this.dragging = true; this.local = this.st.getNormalisedValue(); this.st.sliderDragStarted(); }
  end() { this.dragging = false; this.st.sliderDragEnded(); for (const s of this.subs) s(); }
  subscribe(cb: () => void) { this.subs.add(cb); return () => { this.subs.delete(cb); }; }
}

class ToggleHandle implements ParamHandle {
  readonly orphan = false;
  private subs: Subs = new Set();
  constructor(private st: Juce.ToggleState) { subscribeTo(st.valueChangedEvent, this.subs); }
  get() { return this.st.getValue() ? 1 : 0; }
  set(v: number) { this.st.setValue(v >= 0.5); for (const s of this.subs) s(); }
  begin() {} end() {}
  subscribe(cb: () => void) { this.subs.add(cb); return () => { this.subs.delete(cb); }; }
}

class ComboHandle implements ParamHandle {
  readonly orphan = false;
  private subs: Subs = new Set();
  constructor(private st: Juce.ComboBoxState, private id: ParamId) { subscribeTo(st.valueChangedEvent, this.subs); }
  get() { return fromIndex(PARAM_SPECS[this.id], this.st.getChoiceIndex()); }
  set(v: number) { this.st.setChoiceIndex(toIndex(PARAM_SPECS[this.id], v)); for (const s of this.subs) s(); }
  begin() {} end() {}
  subscribe(cb: () => void) { this.subs.add(cb); return () => { this.subs.delete(cb); }; }
}

// Id che il plugin non espone: il controllo resta visibile ma inerte, cosi' una
// UI piu' nuova del binario non esplode.
class OrphanHandle implements ParamHandle {
  readonly orphan = true;
  constructor(private value: number, id: ParamId) { console.warn(`[juce] parametro "${id}" sconosciuto al plugin: controllo inerte`); }
  get() { return this.value; } set() {} begin() {} end() {}
  subscribe() { return () => {}; }
}

export async function createJuceBackend(): Promise<Backend> {
  // Import dinamico: il modulo vendor legge window.__JUCE__ al caricamento.
  const juce = await import("./vendor/juce-frontend/index.js");
  const init = window.__JUCE__!.initialisationData;
  const handles = new Map<ParamId, ParamHandle>();
  const call = (name: string) => juce.getNativeFunction(name);

  // removeEventListener del bundle vendor upstream è un no-op: registrando un
  // listener per ogni subscribe i callback si accumulerebbero ad ogni remount.
  // Ne registriamo uno solo per evento e distribuiamo a un Set locale, così la
  // funzione di unsubscribe restituita rimuove davvero il callback.
  const fanOut = <T>(event: string) => {
    const subs = new Set<(p: T) => void>();
    window.__JUCE__!.backend.addEventListener(event, (p) => { for (const cb of [...subs]) cb(p as T); });
    return (cb: (p: T) => void) => { subs.add(cb); return () => { subs.delete(cb); }; };
  };
  const onStateChanged = fanOut<BridgeState & { origin: string }>("stateChanged");
  const onMeters = fanOut<MeterFrame>("meters");

  return {
    kind: "juce",
    param(id) {
      let h = handles.get(id);
      if (!h) {
        const spec = PARAM_SPECS[id];
        if (spec.kind === "bool" && init.__juce__toggles.includes(id)) h = new ToggleHandle(juce.getToggleState(id));
        else if (spec.kind === "choice" && init.__juce__comboBoxes.includes(id)) h = new ComboHandle(juce.getComboBoxState(id), id);
        else if ((spec.kind === "float" || spec.kind === "int") && init.__juce__sliders.includes(id)) h = new SliderHandle(juce.getSliderState(id));
        else h = new OrphanHandle(defaultNormalised(spec), id);
        handles.set(id, h);
      }
      return h;
    },
    getState: () => call("getState")() as Promise<BridgeState>,
    setMods: (mods: ModAssignment[], origin: string) => call("setMods")(JSON.stringify(mods), origin).then(() => {}),
    setArpSteps: (steps: number[], origin: string) => call("setArpSteps")(JSON.stringify(steps), origin).then(() => {}),
    loadPreset: (index: number) => call("loadPreset")(index).then(() => {}),
    onStateChanged,
    onMeters,
    async noteOn(note, velocity) { await call("noteOn")(note, velocity); },
    async noteOff(note) { await call("noteOff")(note); },
    async allNotesOff() { await call("allNotesOff")(); },
    async setWheel(kind, value) { await call("setWheel")(kind, value); },
  };
}
