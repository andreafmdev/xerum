import { readFileSync } from "node:fs";
import { join } from "node:path";
import { describe, expect, it } from "vitest";
import { act, fireEvent, render, screen, within } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { FakeBackend } from "../../juce/fake-backend";
import { ZERO_METERS } from "../../juce/backend";
import { BridgeProvider } from "../../juce/provider";
import { H, SynthWindow } from "./SynthWindow";

HTMLCanvasElement.prototype.getContext = (() => null) as unknown as HTMLCanvasElement["getContext"];

function mount(b = new FakeBackend({ state: { mods: [{ src: "lfo", target: "cutoff", depth: 0.25 }, { src: "env", target: "wtpos", depth: 0.3 }] } }), props: Partial<React.ComponentProps<typeof SynthWindow>> = {}) {
  render(<BridgeProvider backend={b}><SynthWindow {...props} /></BridgeProvider>);
  return b;
}

describe("SynthWindow on the bridge", () => {
  it("renders panels, display, meters", async () => {
    mount();
    expect(screen.getByText("Oscillator")).toBeInTheDocument();
    expect(screen.getByRole("img", { name: /wavetable/i })).toBeInTheDocument();
    expect(screen.getByRole("meter", { name: "Output" })).toBeInTheDocument();
  });

  it("gutter=0 lets the chassis fill the whole WebView", async () => {
    // Il nome di prima diceva "so the native keyboard can attach to it": la striscia era una
    // MidiKeyboardComponent montata sotto la WebView, e lo chassis doveva squadrare il proprio
    // fondo perché i due leggessero come un corpo solo. Adesso i tasti sono dentro lo chassis,
    // l'angolo arrotondato è tornato (vedi synth.css) e data-attached serve solo a togliere il
    // margine.
    mount(undefined, { gutter: 0 });
    expect(screen.getByTestId("chassis")).toHaveAttribute("data-attached");
  });

  it("H è 680: i 670 px dei figli più 10 di respiro, il numero che PluginEditor.cpp deve ricalcare", async () => {
    // Non un numero nel test: H e' il valore che guida davvero la scala dello chassis. I figli
    // in flusso sono tutti shrink-0 e si sommano a 670 (padding 20 + Header 40 + WaveDisplay 130
    // + pannelli 224 + TabArea 124 + BottomStrip 108 + quattro gap da 6): l'aritmetica sta nel
    // commento di H in SynthWindow.tsx. Il 708 di prima veniva da "600 di pannello invariato piu'
    // 108 di striscia", ma quei 600 contenevano gia' il Footer da 28 che BottomStrip ha
    // sostituito, e lasciavano 38 px vuoti in fondo.
    //
    // Un fix precedente controllava solo il testo del CSS — cambiare H e lasciare synth.css
    // intatto avrebbe fatto passare quel test con una UI rotta. Anche kChassisHeight in
    // PluginEditor.cpp deve restare uguale a questo numero: tre posti, una sola sorgente di
    // verita'.
    expect(H).toBe(680);
  });

  it("la regola .sx-chassis in synth.css non diverge da H", async () => {
    // toHaveStyle non basta: `test.css: false` in vitest.config.ts fa sì che l'import di
    // synth.css sia stubbato, quindi in jsdom la regola .sx-chassis non viene mai applicata e
    // getComputedStyle non la vedrebbe comunque. Si legge quindi la regola sorgente e si
    // confronta con H, non con un letterale: cosi' le due copie non possono divergere in
    // silenzio, che era il difetto vero da chiudere.
    const css = readFileSync(join(process.cwd(), "src/synth/ui/synth.css"), "utf8");
    const chassisRule = css.match(/\.sx-chassis\s*\{[^}]*\}/)?.[0] ?? "";
    expect(chassisRule).toMatch(new RegExp(`height:\\s*${H}px`));
  });

  it("monta la striscia bassa al posto del footer", async () => {
    mount();
    expect(screen.getAllByTestId("key-white").length).toBeGreaterThan(0);
    expect(screen.getByRole("slider", { name: "PB" })).toBeInTheDocument();
  });

  it("by default the chassis keeps its margin and stays detached", async () => {
    mount();
    expect(screen.getByTestId("chassis")).not.toHaveAttribute("data-attached");
  });

  it("a knob drag sends begin/set/end to the backend and marks the preset dirty", async () => {
    const b = mount();
    const slider = screen.getByRole("slider", { name: "Cutoff" });
    fireEvent.pointerDown(slider, { clientY: 100, clientX: 0, button: 0, pointerId: 1 });
    fireEvent.pointerMove(slider, { clientY: 80, clientX: 0, pointerId: 1 });
    fireEvent.pointerUp(slider, { clientY: 80, clientX: 0, pointerId: 1 });
    const ops = b.log.filter((o) => o.id === "cutoff").map((o) => o.op);
    expect(ops[0]).toBe("begin");
    expect(ops).toContain("set");
    expect(ops.at(-1)).toBe("end");
    expect(screen.getByRole("button", { name: /Init \*/ })).toBeInTheDocument();
  });

  it("double-clicking a knob returns it to the spec default, not to zero", async () => {
    const b = mount();
    const slider = screen.getByRole("slider", { name: "Cutoff" });
    fireEvent.pointerDown(slider, { clientY: 100, clientX: 0, button: 0, pointerId: 1 });
    fireEvent.pointerMove(slider, { clientY: 60, clientX: 0, pointerId: 1 });
    fireEvent.pointerUp(slider, { clientY: 60, clientX: 0, pointerId: 1 });
    expect(b.param("cutoff").get()).not.toBeCloseTo(0.62);
    fireEvent.doubleClick(slider);
    expect(b.param("cutoff").get()).toBeCloseTo(0.62);   // default dello spec, non 0
  });

  it("a host change moves the knob readout", async () => {
    const b = mount();
    act(() => b.push("cutoff", 0));
    expect(screen.getByRole("slider", { name: "Cutoff" })).toHaveAttribute("aria-valuetext", "20 Hz");
  });

  it("choice, int and bool controls write the backend", async () => {
    const b = mount();
    await userEvent.click(screen.getByRole("radio", { name: "HP" }));
    expect(b.param("ftype").get()).toBe(0.5);
    await userEvent.click(screen.getByRole("button", { name: "Increase Octave" }));
    expect(b.param("oct").get()).toBeCloseTo(4 / 6);
    await userEvent.click(screen.getByRole("switch", { name: "Filter on" }));
    expect(b.param("filtOn").get()).toBe(0);
    expect(screen.getByRole("slider", { name: "Cutoff" })).toHaveAttribute("aria-disabled", "true");
    await userEvent.click(screen.getByRole("button", { name: "Bypass" }));
    expect(b.param("bypass").get()).toBe(1);
  });

  it("a segmented change marks the preset dirty", async () => {
    mount();
    await userEvent.click(screen.getByRole("radio", { name: "HP" }));
    expect(screen.getByRole("button", { name: /Init \*/ })).toBeInTheDocument();
  });

  it("mod matrix comes from the bridge state, drop adds through it, remove goes through it", async () => {
    const b = mount(undefined, { initialTab: "mod" });
    await act(async () => {});
    expect(screen.getByText("Filter · Cutoff")).toBeInTheDocument();
    await userEvent.click(screen.getByRole("button", { name: "Remove LFO → Filter · Cutoff" }));
    expect((await b.getState()).mods).toHaveLength(1);
    const knob = screen.getByRole("slider", { name: "Resonance" }).closest("[data-slot=knob]")!;
    const dataTransfer = { types: ["text/x-mod"], getData: () => "env" };
    fireEvent.dragOver(knob, { dataTransfer });
    fireEvent.drop(knob, { dataTransfer });
    expect((await b.getState()).mods).toContainEqual({ src: "env", target: "res", depth: 0.3 });
    expect(screen.getByText("Filter · Resonance")).toBeInTheDocument();
  });

  it("the Envelope tab edits both envelopes: the segmented control rebinds the four knobs", async () => {
    // Il buco che chiude: i quattro parametri di env2 esistono nell'APVTS, e nessun controllo
    // li toccava. Un parametro esposto all'host e irraggiungibile e' il difetto che questa
    // aggiunta non doveva ripetere.
    const b = mount(undefined, { initialTab: "env" });
    const dragAttack = () => {
      const slider = screen.getByRole("slider", { name: "Attack" });
      fireEvent.pointerDown(slider, { clientY: 100, clientX: 0, button: 0, pointerId: 1 });
      fireEvent.pointerMove(slider, { clientY: 60, clientX: 0, pointerId: 1 });
      fireEvent.pointerUp(slider, { clientY: 60, clientX: 0, pointerId: 1 });
    };

    dragAttack();
    expect(b.log.filter((o) => o.id === "att").length).toBeGreaterThan(0);
    expect(b.log.filter((o) => o.id === "att2")).toHaveLength(0);

    await userEvent.click(screen.getByRole("radio", { name: "ENV2" }));
    dragAttack();
    expect(b.log.filter((o) => o.id === "att2").length).toBeGreaterThan(0);

    // I due knob che appartengono al solo inviluppo d'ampiezza restano visibili ma spenti.
    expect(screen.getByRole("slider", { name: "Vel → amp" })).toHaveAttribute("aria-disabled", "true");
  });

  it("the ENV2 chip drops onto a knob like the other four sources", async () => {
    const b = mount(undefined, { initialTab: "mod" });
    await act(async () => {});
    expect(screen.getByTestId("mod-chip-env2")).toBeInTheDocument();
    const knob = screen.getByRole("slider", { name: "Resonance" }).closest("[data-slot=knob]")!;
    const dataTransfer = { types: ["text/x-mod"], getData: () => "env2" };
    fireEvent.dragOver(knob, { dataTransfer });
    fireEvent.drop(knob, { dataTransfer });
    expect((await b.getState()).mods).toContainEqual({ src: "env2", target: "res", depth: 0.3 });
    // La riga nel mod matrix porta l'etichetta della sorgente nuova, non un fallback vuoto.
    expect(screen.getByRole("button", { name: "Remove ENV2 → Filter · Resonance" })).toBeInTheDocument();
  });

  it("external state change updates the matrix", async () => {
    const b = mount(undefined, { initialTab: "mod" });
    await act(async () => {});
    act(() => b.emitStateChanged({ version: 1, mods: [], arpSteps: new Array(16).fill(0) }, "host"));
    expect(screen.queryByText("Filter · Cutoff")).not.toBeInTheDocument();
  });

  it("arp steps persist through the bridge", async () => {
    const b = mount(undefined, { initialTab: "arp" });
    await act(async () => {});
    await userEvent.click(screen.getByRole("button", { name: "Step 2: off" }));
    expect((await b.getState()).arpSteps[1]).toBe(0.8);
  });

  it("meters follow the backend", () => {
    const b = mount();
    act(() => b.emitMeters({ ...ZERO_METERS, in: 1, out: 1 }));
    expect(within(screen.getByRole("meter", { name: "Output" })).getAllByTestId("meter-segment").filter((s) => s.dataset.lit === "true")).toHaveLength(24);
  });

  it("preset browsing stays local", async () => {
    mount();
    await userEvent.click(screen.getByRole("button", { name: /Init/ }));
    const overlay = screen.getByRole("dialog", { name: "Presets" });
    await userEvent.type(within(overlay).getByRole("searchbox"), "acid");
    await userEvent.click(within(overlay).getByRole("button", { name: /Acid Line/ }));
    expect(screen.getByRole("button", { name: /Acid Line/ })).toBeInTheDocument();
  });

  it("picking a preset shows the clean name, not the dirty marker", async () => {
    mount();
    await userEvent.click(screen.getByRole("button", { name: /Init/ }));
    const overlay = screen.getByRole("dialog", { name: "Presets" });
    await userEvent.type(within(overlay).getByRole("searchbox"), "acid");
    await userEvent.click(within(overlay).getByRole("button", { name: /Acid Line/ }));
    // Ancorato a fine stringa: "Acid Line *" (sporco) non termina con "Acid Line"
    // e farebbe fallire questa asserzione, a differenza di un /Acid Line/ non ancorato.
    expect(screen.getByRole("button", { name: /Acid Line$/ })).toBeInTheDocument();
    expect(screen.queryByRole("button", { name: /\*/ })).not.toBeInTheDocument();
  });
});
