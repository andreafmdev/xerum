import { beforeEach, describe, expect, it, vi } from "vitest";

type Listener = (p: unknown) => void;

// Il modulo vendor legge window.__JUCE__ al momento dell'import: lo stub va
// installato prima di ogni import dinamico, e i moduli vanno resettati.
function installJuceStub() {
  const listeners = new Map<string, Listener[]>();
  const emitted: { id: string; p: any }[] = [];
  const backend = {
    addEventListener: (id: string, fn: Listener) => { listeners.set(id, [...(listeners.get(id) ?? []), fn]); return listeners.size; },
    removeEventListener: () => {},
    emitEvent: (id: string, p: any) => {
      emitted.push({ id, p });
      // Il backend vero risponde a requestInitialUpdate con properties + value: simuliamo cutoff 0..1 e slope combo.
      if (p?.eventType === "requestInitialUpdate") {
        const fire = (payload: unknown) => listeners.get(id)?.forEach((fn) => fn(payload));
        if (id === "__juce__slidercutoff") { fire({ eventType: "propertiesChanged", start: 0, end: 1, skew: 1, interval: 0, name: "Cutoff", label: "Hz", numSteps: 100, parameterIndex: 3 }); fire({ eventType: "valueChanged", value: 0.62 }); }
        if (id === "__juce__slideroct") { fire({ eventType: "propertiesChanged", start: -3, end: 3, skew: 1, interval: 1, name: "Octave", label: "", numSteps: 7, parameterIndex: 4 }); fire({ eventType: "valueChanged", value: 0 }); }
        if (id === "__juce__comboBoxslope") { fire({ eventType: "propertiesChanged", name: "Slope", parameterIndex: 5, choices: ["12", "24"] }); fire({ eventType: "valueChanged", value: 1 }); }
        if (id === "__juce__togglefiltOn") { fire({ eventType: "propertiesChanged", name: "Filter on", parameterIndex: 6 }); fire({ eventType: "valueChanged", value: true }); }
      }
      if (id === "__juce__invoke" && p.name === "getState")
        listeners.get("__juce__complete")?.forEach((fn) => fn({ promiseId: p.resultId, result: { version: 1, mods: [], arpSteps: new Array(16).fill(0) } }));
    },
  };
  (window as any).__JUCE__ = { backend, initialisationData: { __juce__sliders: ["cutoff", "oct"], __juce__toggles: ["filtOn"], __juce__comboBoxes: ["slope"], __juce__functions: ["getState", "setMods", "setArpSteps"] } };
  return { listeners, emitted };
}

describe("JuceBackend", () => {
  let stub: ReturnType<typeof installJuceStub>;
  beforeEach(() => { vi.resetModules(); stub = installJuceStub(); });

  it("normalises float, int, choice and bool handles to 0..1", async () => {
    const { createJuceBackend } = await import("./juce-backend");
    const b = await createJuceBackend();
    expect(b.param("cutoff").get()).toBeCloseTo(0.62);
    expect(b.param("oct").get()).toBe(0.5);
    expect(b.param("slope").get()).toBe(1);
    expect(b.param("filtOn").get()).toBe(1);
  });

  it("set/begin/end reach the relay with scaled values", async () => {
    const { createJuceBackend } = await import("./juce-backend");
    const b = await createJuceBackend();
    const h = b.param("oct");
    h.begin(); h.set(1); h.end();
    const ids = stub.emitted.filter((e) => e.id === "__juce__slideroct").map((e) => e.p.eventType);
    expect(ids).toEqual(["requestInitialUpdate", "sliderDragStarted", "valueChanged", "sliderDragEnded"]);
    expect(stub.emitted.find((e) => e.id === "__juce__slideroct" && e.p.eventType === "valueChanged")!.p.value).toBe(3);
    b.param("slope").set(0);
    expect(stub.emitted.at(-1)).toEqual({ id: "__juce__comboBoxslope", p: { eventType: "valueChanged", value: 0 } });
  });

  it("host changes notify subscribers; echoes during a gesture are ignored", async () => {
    const { createJuceBackend } = await import("./juce-backend");
    const b = await createJuceBackend();
    const h = b.param("cutoff");
    const cb = vi.fn(); h.subscribe(cb);
    const fire = (v: number) => stub.listeners.get("__juce__slidercutoff")!.forEach((fn) => fn({ eventType: "valueChanged", value: v }));
    fire(0.3); expect(cb).toHaveBeenCalledTimes(1); expect(h.get()).toBeCloseTo(0.3);
    h.begin(); h.set(0.9); fire(0.1); expect(h.get()).toBeCloseTo(0.9); h.end(); expect(h.get()).toBeCloseTo(0.1);
  });

  it("local set notifies subscribers immediately", async () => {
    const { createJuceBackend } = await import("./juce-backend");
    const b = await createJuceBackend();
    // setNormalisedValue/setValue/setChoiceIndex non fanno scattare i listener del
    // relay: senza la notifica locale il knob aspetterebbe l'eco del C++.
    const slider = vi.fn(); b.param("cutoff").subscribe(slider);
    b.param("cutoff").set(0.8);
    expect(slider).toHaveBeenCalledTimes(1);
    const toggle = vi.fn(); b.param("filtOn").subscribe(toggle);
    b.param("filtOn").set(0);
    expect(toggle).toHaveBeenCalledTimes(1);
    const combo = vi.fn(); b.param("slope").subscribe(combo);
    b.param("slope").set(0);
    expect(combo).toHaveBeenCalledTimes(1);
  });

  it("stops notifying after unsubscribe", async () => {
    const { createJuceBackend } = await import("./juce-backend");
    const b = await createJuceBackend();
    const h = b.param("cutoff");
    const cb = vi.fn();
    const off = h.subscribe(cb);
    const fire = (v: number) => stub.listeners.get("__juce__slidercutoff")!.forEach((fn) => fn({ eventType: "valueChanged", value: v }));
    fire(0.3); expect(cb).toHaveBeenCalledTimes(1);
    off();
    fire(0.4); expect(cb).toHaveBeenCalledTimes(1);
  });

  it("registers one real listener per event: two subscribers, one unsubscribes, only the other is called", async () => {
    const { createJuceBackend } = await import("./juce-backend");
    const b = await createJuceBackend();
    const first = vi.fn();
    const second = vi.fn();
    const off = b.onStateChanged(first);
    b.onStateChanged(second);
    // removeEventListener upstream è un no-op: un solo addEventListener reale, il
    // fan-out e l'unsubscribe restano locali al backend.
    expect(stub.listeners.get("stateChanged")).toHaveLength(1);
    const fire = () => stub.listeners.get("stateChanged")!.forEach((fn) => fn({ version: 1, mods: [], arpSteps: [], origin: "x" }));
    fire();
    expect(first).toHaveBeenCalledTimes(1);
    expect(second).toHaveBeenCalledTimes(1);
    off();
    fire();
    expect(first).toHaveBeenCalledTimes(1);
    expect(second).toHaveBeenCalledTimes(2);
  });

  it("marks unknown ids as orphans with a no-op set", async () => {
    const warn = vi.spyOn(console, "warn").mockImplementation(() => {});
    const { createJuceBackend } = await import("./juce-backend");
    const b = await createJuceBackend();
    const h = b.param("res");
    expect(h.orphan).toBe(true);
    h.set(0.9);
    expect(h.get()).toBeCloseTo(0.3);   // default dello spec
    expect(b.param("res")).toBe(h);     // handle memoizzato
    expect(warn).toHaveBeenCalledTimes(1);
    warn.mockRestore();
  });

  it("getState calls the native function and resolves", async () => {
    const { createJuceBackend } = await import("./juce-backend");
    const b = await createJuceBackend();
    expect((await b.getState()).arpSteps).toHaveLength(16);
  });

  it("setMods sends JSON and origin; stateChanged/meters events are forwarded", async () => {
    const { createJuceBackend } = await import("./juce-backend");
    const b = await createJuceBackend();
    const st = vi.fn(); b.onStateChanged(st);
    const mt = vi.fn(); b.onMeters(mt);
    void b.setMods([{ src: "lfo", target: "cutoff", depth: 0.2 }], "abc");
    const call = stub.emitted.find((e) => e.id === "__juce__invoke" && e.p.name === "setMods")!;
    expect(call.p.params).toEqual([JSON.stringify([{ src: "lfo", target: "cutoff", depth: 0.2 }]), "abc"]);
    stub.listeners.get("stateChanged")!.forEach((fn) => fn({ version: 1, mods: [], arpSteps: [], origin: "x" }));
    stub.listeners.get("meters")!.forEach((fn) => fn({ in: 0.1, out: 0.2, lfo: 0, arpStep: 0 }));
    expect(st).toHaveBeenCalledWith(expect.objectContaining({ origin: "x" }));
    expect(mt).toHaveBeenCalledWith({ in: 0.1, out: 0.2, lfo: 0, arpStep: 0 });
  });

  it("setArpSteps sends JSON and origin", async () => {
    const { createJuceBackend } = await import("./juce-backend");
    const b = await createJuceBackend();
    const steps = new Array(16).fill(0).map((_, i) => i / 15);
    void b.setArpSteps(steps, "zz");
    const call = stub.emitted.find((e) => e.id === "__juce__invoke" && e.p.name === "setArpSteps")!;
    expect(call.p.params).toEqual([JSON.stringify(steps), "zz"]);
  });

  it("hasJuce reflects the presence of window.__JUCE__.backend", async () => {
    const { hasJuce } = await import("./juce-backend");
    expect(hasJuce()).toBe(true);
    delete (window as any).__JUCE__;
    expect(hasJuce()).toBe(false);
  });
});
