// Backend in memoria: gira nel browser senza JUCE e nei test. Riproduce il
// contratto di Backend con uno stato locale, un log di gesture per i test e,
// opzionalmente, un clock rAF che simula meter e LFO (ex useClock).

import { PARAM_IDS, type ParamId } from "../synth/params.generated";
import { PRESETS } from "../synth/presets.generated";
import { defaultNormalised, specOf, type Backend, type BridgeState, type MeterFrame, type MidiInputs, type ModAssignment, type ParamHandle } from "./backend";

const clamp01 = (v: number) => Math.min(1, Math.max(0, v));
type Op = { id: ParamId; op: "begin" | "set" | "end"; v?: number };

class FakeHandle implements ParamHandle {
  readonly orphan = false;
  private value: number;
  private subs = new Set<() => void>();
  constructor(private id: ParamId, initial: number, private log: Op[]) { this.value = initial; }
  get() { return this.value; }
  set(v: number) { this.value = clamp01(v); this.log.push({ id: this.id, op: "set", v: this.value }); this.notify(); }
  begin() { this.log.push({ id: this.id, op: "begin" }); }
  end() { this.log.push({ id: this.id, op: "end" }); }
  subscribe(cb: () => void) { this.subs.add(cb); return () => { this.subs.delete(cb); }; }
  /** Simula un cambio dall'host. */
  push(v: number) { this.value = clamp01(v); this.notify(); }
  private notify() { for (const s of this.subs) s(); }
}

export class FakeBackend implements Backend {
  readonly kind = "fake" as const;
  readonly log: Op[] = [];
  private handles = new Map<ParamId, FakeHandle>();
  private state: BridgeState;
  private stateSubs = new Set<(s: BridgeState & { origin: string }) => void>();
  private meterSubs = new Set<(m: MeterFrame) => void>();
  private raf = 0;
  /** Le note che la UI sta tenendo premute. Pubblico: è ciò su cui i test guardano. */
  readonly playing = new Set<number>();
  wheels = { pitch: 0.5, mod: 0 };
  /** Cosa la UI ha chiesto di abilitare/disabilitare, in ordine. */
  readonly midiLog: [string, boolean][] = [];
  private midi: MidiInputs = { host: true, devices: [] };
  private midiSubs = new Set<(m: MidiInputs) => void>();

  constructor(opts: { demo?: boolean; state?: Partial<BridgeState>; values?: Partial<Record<ParamId, number>>; midiInputs?: MidiInputs } = {}) {
    if (opts.midiInputs) this.midi = structuredClone(opts.midiInputs);
    this.state = { version: 1, mods: [], arpSteps: [0.8, 0, 0.6, 0.9, 0, 0.7, 0, 0.5, 0.8, 0, 0.6, 0, 0.9, 0.4, 0, 0.7], ...opts.state };
    for (const id of PARAM_IDS) this.handles.set(id, new FakeHandle(id, opts.values?.[id] ?? defaultNormalised(specOf(id)), this.log));
    if (opts.demo && typeof requestAnimationFrame === "function") this.startDemo();
  }

  param(id: ParamId): ParamHandle { return this.handles.get(id)!; }
  /** Per i test: cambio "dall'host". */
  push(id: ParamId, v: number) { this.handles.get(id)!.push(v); }

  async getState() { return structuredClone(this.state); }
  async setMods(mods: ModAssignment[], origin: string) { this.state = { ...this.state, mods: [...mods] }; this.emitStateChanged(this.state, origin); }
  async setArpSteps(steps: number[], origin: string) { this.state = { ...this.state, arpSteps: [...steps] }; this.emitStateChanged(this.state, origin); }
  // Rispecchia Source/bridge/StateChannel.cpp::applyPreset: un parametro non elencato
  // nel preset torna al suo default di spec, cosi' il demo nel browser si comporta come
  // il plugin. Le due implementazioni possono divergere senza che nessun test se ne accorga
  // (XerumTests non compila Source/bridge/): se cambi questa logica, cambia anche l'altra.
  async loadPreset(index: number) {
    const preset = PRESETS[index];
    if (!preset) return;
    for (const id of PARAM_IDS)
      this.handles.get(id)!.push(preset.values[id] ?? defaultNormalised(specOf(id)));
    this.emitStateChanged(this.state, "preset");
  }
  onStateChanged(cb: (s: BridgeState & { origin: string }) => void) { this.stateSubs.add(cb); return () => { this.stateSubs.delete(cb); }; }
  onMeters(cb: (m: MeterFrame) => void) { this.meterSubs.add(cb); return () => { this.meterSubs.delete(cb); }; }
  emitStateChanged(s: BridgeState, origin: string) { for (const cb of this.stateSubs) cb({ ...structuredClone(s), origin }); }
  emitMeters(m: MeterFrame) { for (const cb of this.meterSubs) cb(m); }
  /** Quanti listener di meter sono attaccati: lo store ne deve tenere uno solo. */
  meterListeners() { return this.meterSubs.size; }

  async noteOn(note: number, _velocity: number) { this.playing.add(note); }
  async noteOff(note: number) { this.playing.delete(note); }
  async allNotesOff() { this.playing.clear(); }
  async midiInputs() { return structuredClone(this.midi); }
  async setMidiInputEnabled(id: string, enabled: boolean) { this.midiLog.push([id, enabled]); }
  onMidiInputsChanged(cb: (m: MidiInputs) => void) { this.midiSubs.add(cb); return () => { this.midiSubs.delete(cb); }; }
  /** Per i test: la lista cambia "dal sistema". */
  emitMidiInputsChanged(m: MidiInputs) { this.midi = structuredClone(m); for (const cb of this.midiSubs) cb(structuredClone(m)); }
  async setWheel(kind: "pitch" | "mod", value: number) { this.wheels = { ...this.wheels, [kind]: value }; }

  /**
   * Clock finto per browser/Storybook: LFO, meter che respirano, step arp e i livelli delle
   * cinque sorgenti del mod matrix.
   *
   * Le sorgenti non sono rumore decorativo: senza host è l'unico modo di vedere se un anello di
   * modulazione si muove davvero. Quindi si finge una nota ogni mezzo secondo e le si dà un
   * inviluppo — `env` con attacco corto e decadimento, `env2` più lento e più tondo, `vel` che
   * cambia da nota a nota e resta ferma mentre la nota dura, `mw` che va avanti e indietro come
   * una rotella mossa a mano. Sono tutte unipolari 0..1 come quelle vere; l'LFO resta bipolare.
   */
  private startDemo() {
    let last = 0;
    const tick = (now: number) => {
      if (now - last > 33) {
        last = now;
        const t = now / 1000;
        const audio = 0.55 + 0.25 * Math.sin(t * 1.7) + 0.1 * Math.sin(t * 7.3);
        const rate = 0.05 * Math.pow(400, this.param("lrate").get());

        const note = Math.floor(t * 2);          // due note al secondo
        const since = t * 2 - note;              // 0..1 dentro la nota
        const env = since < 0.08 ? since / 0.08 : 0.35 + 0.65 * Math.exp(-6 * (since - 0.08));
        const env2 = Math.sin(Math.min(1, since * 1.4) * Math.PI * 0.5);

        // La stessa nota finta che muove env/vel accende il suo bit: senza, in Storybook la
        // tastiera resterebbe spenta mentre tutto il resto respira. Questo clock non conosce un
        // "rilascio" — le note finte si susseguono senza pause — quindi la nota corrente e'
        // sempre accesa, non solo durante l'attacco.
        const fakeNote = 48 + (note % 12);
        const bits = [0, 0, 0, 0];
        bits[fakeNote >> 5] |= 1 << (fakeNote & 31);
        // Le note premute dalla UI (click sulla tastiera in Storybook) si aggiungono
        // a quella finta del clock, non la sostituiscono.
        for (const n of this.playing) bits[n >> 5] |= 1 << (n & 31);

        this.emitMeters({
          in: audio * this.param("level").get(),
          out: audio * this.param("volume").get(),
          lfo: Math.sin(t * rate * 2 * Math.PI),
          env,
          env2,
          vel: 0.55 + 0.4 * Math.sin(note * 2.1),
          mw: 0.5 + 0.5 * Math.sin(t * 0.3),
          arpStep: Math.floor(t * 8) % 16,
          n0: bits[0]!, n1: bits[1]!, n2: bits[2]!, n3: bits[3]!,
        });
      }
      this.raf = requestAnimationFrame(tick);
    };
    this.raf = requestAnimationFrame(tick);
  }
  dispose() { if (this.raf) cancelAnimationFrame(this.raf); }
}
