import { describe, expect, it } from "vitest";
import { fireEvent, render, screen, within } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { SynthWindow } from "./SynthWindow";

// jsdom non ha canvas: il display disegna solo se getContext esiste.
HTMLCanvasElement.prototype.getContext = (() => null) as unknown as HTMLCanvasElement["getContext"];

describe("SynthWindow", () => {
  it("renders the three fixed panels, the display and the footer meters", () => {
    render(<SynthWindow animate={false} />);
    expect(screen.getByText("Oscillator")).toBeInTheDocument();
    expect(screen.getByText("Filter")).toBeInTheDocument();
    expect(screen.getByText("Master")).toBeInTheDocument();
    expect(screen.getByRole("img", { name: /wavetable/i })).toBeInTheDocument();
    expect(screen.getByRole("meter", { name: "Input" })).toBeInTheDocument();
    expect(screen.getByRole("meter", { name: "Output" })).toBeInTheDocument();
  });

  it("sets the variant on the chassis", () => {
    render(<SynthWindow variant="glow" animate={false} />);
    expect(screen.getByTestId("chassis")).toHaveAttribute("data-variant", "glow");
  });

  it("opens on the initial tab and switches", async () => {
    render(<SynthWindow animate={false} initialTab="lfo" />);
    expect(screen.getByRole("tab", { name: "LFO" })).toHaveAttribute("aria-selected", "true");
    expect(screen.getByRole("radiogroup", { name: "LFO shape" })).toBeInTheDocument();
    await userEvent.click(screen.getByRole("tab", { name: "Effects" }));
    expect(screen.getByText("Chorus")).toBeInTheDocument();
    await userEvent.click(screen.getByRole("tab", { name: "Arpeggiator" }));
    expect(screen.getAllByRole("button", { name: /^Step \d+/ })).toHaveLength(16);
  });

  it("lists the default mod assignments in the matrix and removes one", async () => {
    render(<SynthWindow animate={false} initialTab="mod" />);
    expect(screen.getByText("Filter · Cutoff")).toBeInTheDocument();
    expect(screen.getByText("Osc · Position")).toBeInTheDocument();
    await userEvent.click(screen.getByRole("button", { name: "Remove LFO → Filter · Cutoff" }));
    expect(screen.queryByText("Filter · Cutoff")).not.toBeInTheDocument();
  });

  it("assigns a source by dropping a chip on a knob and jumps to the matrix", () => {
    render(<SynthWindow animate={false} initialTab="env" />);
    const knob = screen.getByRole("slider", { name: "Resonance" }).closest("[data-slot=knob]")!;
    const dataTransfer = { types: ["text/x-mod"], getData: () => "env" };
    fireEvent.dragOver(knob, { dataTransfer });
    fireEvent.drop(knob, { dataTransfer });
    expect(screen.getByRole("tab", { name: "Mod matrix" })).toHaveAttribute("aria-selected", "true");
    expect(screen.getByText("Filter · Resonance")).toBeInTheDocument();
  });

  it("browses presets: opens the overlay, filters, picks, closes", async () => {
    render(<SynthWindow animate={false} />);
    await userEvent.click(screen.getByRole("button", { name: /Glass Pad/ }));
    const overlay = screen.getByRole("dialog", { name: "Presets" });
    await userEvent.type(within(overlay).getByRole("searchbox"), "acid");
    await userEvent.click(within(overlay).getByRole("button", { name: /Acid Line/ }));
    expect(screen.queryByRole("dialog")).not.toBeInTheDocument();
    expect(screen.getByRole("button", { name: /Acid Line/ })).toBeInTheDocument();
  });

  it("steps presets with the arrows and marks edits dirty", async () => {
    render(<SynthWindow animate={false} />);
    await userEvent.click(screen.getByRole("button", { name: "Next preset" }));
    expect(screen.getByRole("button", { name: /Sub Pulse/ })).toBeInTheDocument();
    fireEvent.keyDown(screen.getByRole("slider", { name: "Cutoff" }), { key: "ArrowUp" });
    expect(screen.getByRole("button", { name: /Sub Pulse \*/ })).toBeInTheDocument();
  });

  it("switches the filter off from the header LED and disables its knobs", async () => {
    render(<SynthWindow animate={false} />);
    await userEvent.click(screen.getByRole("switch", { name: "Filter on" }));
    expect(screen.getByRole("switch", { name: "Filter on" })).toHaveAttribute("aria-checked", "false");
    expect(screen.getByRole("slider", { name: "Cutoff" })).toHaveAttribute("aria-disabled", "true");
  });

  it("toggles bypass", async () => {
    render(<SynthWindow animate={false} />);
    const bypass = screen.getByRole("button", { name: "Bypass" });
    expect(bypass).toHaveAttribute("aria-pressed", "false");
    await userEvent.click(bypass);
    expect(bypass).toHaveAttribute("aria-pressed", "true");
  });
});
