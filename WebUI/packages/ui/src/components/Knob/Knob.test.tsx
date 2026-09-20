import { describe, expect, it, vi } from "vitest";
import { fireEvent, render, screen } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { Knob } from "./Knob";

describe("Knob", () => {
  it("is an accessible slider with normalised range", () => {
    render(<Knob value={0.25} onChange={() => {}} label="Cutoff" />);
    const slider = screen.getByRole("slider", { name: "Cutoff" });
    expect(slider).toHaveAttribute("aria-valuemin", "0");
    expect(slider).toHaveAttribute("aria-valuemax", "1");
    expect(slider).toHaveAttribute("aria-valuenow", "0.25");
    expect(slider).toHaveAttribute("tabindex", "0");
  });

  it("uses format for the readout and aria-valuetext", () => {
    render(<Knob value={0.5} onChange={() => {}} label="Cutoff" format={(v) => `${Math.round(v * 20000)} Hz`} />);
    expect(screen.getByRole("slider")).toHaveAttribute("aria-valuetext", "10000 Hz");
    expect(screen.getByTestId("knob-readout")).toHaveTextContent("10000 Hz");
  });

  it("defaults the readout to a percentage", () => {
    render(<Knob value={0.5} onChange={() => {}} label="Level" />);
    expect(screen.getByTestId("knob-readout")).toHaveTextContent("50%");
  });

  it("keeps the value readable at rest, without a hover-only overlay", () => {
    render(<Knob value={0.4} onChange={() => {}} label="Cutoff" />);
    const readout = screen.getByTestId("knob-readout");
    expect(readout).toBeVisible();
    expect(readout.className).not.toContain("opacity-0");
    expect(readout).toHaveTextContent("40%");
  });

  it("changes value with the keyboard", () => {
    const onChange = vi.fn();
    render(<Knob value={0.5} onChange={onChange} label="Cutoff" />);
    fireEvent.keyDown(screen.getByRole("slider"), { key: "ArrowUp" });
    expect(onChange).toHaveBeenCalledWith(expect.closeTo(0.51, 5));
  });

  it("marks dragging state", () => {
    render(<Knob value={0.5} onChange={() => {}} label="Cutoff" />);
    const slider = screen.getByRole("slider");
    expect(slider).toHaveAttribute("data-dragging", "false");
    fireEvent.pointerDown(slider, { clientY: 0, clientX: 0, button: 0, pointerId: 1 });
    expect(slider).toHaveAttribute("data-dragging", "true");
  });

  it("calls onChangeEnd after a drag ends", () => {
    const onChangeEnd = vi.fn();
    render(<Knob value={0.5} onChange={() => {}} onChangeEnd={onChangeEnd} label="Cutoff" />);
    const slider = screen.getByRole("slider");
    fireEvent.pointerDown(slider, { clientY: 100, clientX: 0, button: 0, pointerId: 1 });
    fireEvent.pointerMove(slider, { clientY: 80, clientX: 0, pointerId: 1 });
    expect(onChangeEnd).not.toHaveBeenCalled();
    fireEvent.pointerUp(slider, { clientY: 80, clientX: 0, pointerId: 1 });
    expect(onChangeEnd).toHaveBeenCalledTimes(1);
  });

  it("draws the value arc from the centre when bipolar", () => {
    const { rerender } = render(<Knob value={0.5} onChange={() => {}} label="Pan" bipolar />);
    const arc = () => screen.getByTestId("knob-value-arc").getAttribute("d") ?? "";
    // 0.5 bipolar = arco nullo: inizio e fine coincidono in alto (20, 4)
    expect(arc()).toMatch(/^M 20 4 A 16 16 0 0 1 20 4$/);
    rerender(<Knob value={1} onChange={() => {}} label="Pan" bipolar />);
    expect(arc().startsWith("M 20 4")).toBe(true);
  });

  it("blocks input and exposes aria-disabled when disabled", () => {
    const onChange = vi.fn();
    render(<Knob value={0.5} onChange={onChange} label="Cutoff" disabled />);
    const slider = screen.getByRole("slider");
    expect(slider).toHaveAttribute("aria-disabled", "true");
    fireEvent.keyDown(slider, { key: "End" });
    expect(onChange).not.toHaveBeenCalled();
  });

  it("sets --tone from the tone prop", () => {
    render(<Knob value={0.5} onChange={() => {}} label="Cutoff" tone="filter" />);
    expect(screen.getByTestId("knob").style.getPropertyValue("--tone")).toBe("var(--color-filter)");
  });
});

describe("Knob modulation", () => {
  it("draws one modulation arc per mod, coloured by the mod tone", () => {
    render(
      <Knob
        value={0.5}
        onChange={() => {}}
        label="Cutoff"
        mods={[
          { tone: "lfo", depth: 0.25, bipolar: true },
          { tone: "env", depth: 0.3 },
        ]}
      />,
    );
    const arcs = screen.getAllByTestId("knob-mod-arc");
    expect(arcs).toHaveLength(2);
    expect(arcs[0]!.style.getPropertyValue("stroke")).toBe("var(--color-lfo)");
    expect(arcs[1]!.style.getPropertyValue("stroke")).toBe("var(--color-env)");
  });

  it("spans a bipolar mod both sides of the value and a unipolar one forward only", () => {
    render(
      <Knob
        value={0.5}
        onChange={() => {}}
        label="Cutoff"
        mods={[
          { tone: "lfo", depth: 0.25, bipolar: true },
          { tone: "env", depth: 0.25 },
        ]}
      />,
    );
    const [bi, uni] = screen.getAllByTestId("knob-mod-arc");
    // bipolar: 0.25..0.75 → angoli 202.5..337.5 (135 + 270·v)
    expect(bi!.getAttribute("data-range")).toBe("0.25,0.75");
    // unipolar: 0.5..0.75
    expect(uni!.getAttribute("data-range")).toBe("0.5,0.75");
  });

  it("clamps the modulation range to 0..1", () => {
    render(<Knob value={0.9} onChange={() => {}} label="Cutoff" mods={[{ tone: "env", depth: 0.5 }]} />);
    expect(screen.getByTestId("knob-mod-arc").getAttribute("data-range")).toBe("0.9,1");
  });

  it("shows a live dot at liveValue only when modulated", () => {
    const { rerender } = render(<Knob value={0.5} onChange={() => {}} label="Cutoff" liveValue={0.6} />);
    expect(screen.queryByTestId("knob-live")).not.toBeInTheDocument();
    rerender(<Knob value={0.5} onChange={() => {}} label="Cutoff" liveValue={0.6} mods={[{ tone: "lfo", depth: 0.2 }]} />);
    const dot = screen.getByTestId("knob-live");
    expect(dot).toBeInTheDocument();
    expect(dot.style.getPropertyValue("fill")).toBe("var(--color-lfo)");
  });

  it("accepts a text/x-mod drop and reports the payload", () => {
    const onDropMod = vi.fn();
    render(<Knob value={0.5} onChange={() => {}} label="Cutoff" onDropMod={onDropMod} />);
    const knob = screen.getByTestId("knob");
    const dataTransfer = { types: ["text/x-mod"], getData: () => "lfo" };
    fireEvent.dragOver(knob, { dataTransfer });
    expect(knob).toHaveAttribute("data-drop-target", "true");
    fireEvent.drop(knob, { dataTransfer });
    expect(onDropMod).toHaveBeenCalledWith("lfo");
    expect(knob).toHaveAttribute("data-drop-target", "false");
  });

  it("ignores drags that are not modulation sources", () => {
    const onDropMod = vi.fn();
    render(<Knob value={0.5} onChange={() => {}} label="Cutoff" onDropMod={onDropMod} />);
    const knob = screen.getByTestId("knob");
    fireEvent.dragOver(knob, { dataTransfer: { types: ["text/plain"], getData: () => "x" } });
    expect(knob).toHaveAttribute("data-drop-target", "false");
  });

  it("can hide the readout while keeping aria-valuetext", () => {
    render(<Knob value={0.5} onChange={() => {}} label="Cutoff" hideValue />);
    expect(screen.queryByTestId("knob-readout")).not.toBeInTheDocument();
    expect(screen.getByRole("slider")).toHaveAttribute("aria-valuetext", "50%");
  });
});

describe("Knob motion", () => {
  it("animates the value arc only when the change did not come from the pointer", () => {
    render(<Knob value={0.3} onChange={() => {}} label="Cutoff" />);
    const arc = screen.getByTestId("knob-value-arc");
    expect(arc.getAttribute("class")).toContain("transition-[d,stroke-dashoffset]");
    // Durante il drag la transizione sparisce: il valore insegue il dito, non una curva.
    expect(arc.getAttribute("class")).toContain("group-data-[dragging=true]/knob:transition-none");
  });

  it("settles the cap, never the value", () => {
    render(<Knob value={0.3} onChange={() => {}} label="Cutoff" />);
    expect(screen.getByTestId("knob").querySelector('[data-part="cap"]')?.getAttribute("class")).toContain("ease-settle");
    expect(screen.getByTestId("knob-value-arc").getAttribute("class")).not.toContain("ease-settle");
  });

  it("never transitions the live value dot: it already arrives at 30 Hz", () => {
    render(<Knob value={0.3} onChange={() => {}} label="Cutoff" mods={[{ tone: "lfo", depth: 0.2 }]} liveValue={0.4} />);
    expect(screen.getByTestId("knob-live").getAttribute("class") ?? "").not.toContain("transition");
  });

  it("marks the drag on the root so CSS can switch the rule", async () => {
    // Nota: si trascina sul quadrante (role=slider), non sul contenitore esterno con
    // data-testid="knob" (che porta solo etichetta e readout) — ma e' il contenitore esterno
    // a portare `group/knob`, quindi e' li' che `data-dragging` deve comparire perche' il
    // selettore CSS `group-data-[dragging=true]/knob:…` lo trovi.
    render(<Knob value={0.3} onChange={() => {}} label="Cutoff" />);
    const knob = screen.getByTestId("knob");
    expect(knob).toHaveAttribute("data-dragging", "false");
    await userEvent.pointer([{ keys: "[MouseLeft>]", target: screen.getByRole("slider") }]);
    expect(knob).toHaveAttribute("data-dragging", "true");
  });

  it("lights the drop target on a transition, not on a cut", () => {
    // Sul quadrante (`knob-dial`), non sul contenitore esterno (`knob`): e' li' che il ring
    // `group-data-[drop-target=true]/knob:ring-2` mette davvero il box-shadow, quindi e' li'
    // che la transizione deve stare perche' abbia un effetto — non un contratto sul testid
    // sbagliato che passerebbe anche con CSS morto.
    render(<Knob value={0.3} onChange={() => {}} label="Cutoff" onDropMod={() => {}} />);
    const cls = screen.getByTestId("knob-dial").className;
    expect(cls).toContain("transition-[box-shadow]");
    expect(cls).toContain("duration-(--dur-state)");
    expect(cls).toContain("ease-glass");
  });

  it("grows the modulation ring from the arc when it appears", () => {
    render(<Knob value={0.3} onChange={() => {}} label="Cutoff" mods={[{ tone: "lfo", depth: 0.2 }]} />);
    const cls = screen.getByTestId("knob-mod-arc").getAttribute("class") ?? "";
    expect(cls).toContain("origin-center");
    expect(cls).toContain("motion-safe:animate-[sx-ring-in_var(--dur-state)_var(--ease-glass)]");
  });
});
