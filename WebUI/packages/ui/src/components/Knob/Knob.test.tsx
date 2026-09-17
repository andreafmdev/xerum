import { describe, expect, it, vi } from "vitest";
import { fireEvent, render, screen } from "@testing-library/react";
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
