import { describe, expect, it, vi } from "vitest";
import { fireEvent, render, screen } from "@testing-library/react";
import { Fader } from "./Fader";

describe("Fader", () => {
  it("is a vertical slider by default", () => {
    render(<Fader value={0.5} onChange={() => {}} label="Level" />);
    const s = screen.getByRole("slider", { name: "Level" });
    expect(s).toHaveAttribute("aria-orientation", "vertical");
    expect(s).toHaveAttribute("aria-valuenow", "0.5");
  });

  it("supports horizontal orientation and x-axis drag", () => {
    const onChange = vi.fn();
    render(<Fader value={0.5} onChange={onChange} orientation="horizontal" />);
    const s = screen.getByRole("slider");
    expect(s).toHaveAttribute("aria-orientation", "horizontal");
    fireEvent.pointerDown(s, { clientX: 100, clientY: 0, button: 0, pointerId: 1 });
    fireEvent.pointerMove(s, { clientX: 140, clientY: 0, pointerId: 1 });
    expect(onChange).toHaveBeenLastCalledWith(expect.closeTo(0.7, 5));
  });

  it("resets on double click", () => {
    const onChange = vi.fn();
    render(<Fader value={0.8} defaultValue={0.5} onChange={onChange} />);
    fireEvent.doubleClick(screen.getByRole("slider"));
    expect(onChange).toHaveBeenCalledWith(0.5);
  });

  it("sizes the fill from the value", () => {
    render(<Fader value={0.25} onChange={() => {}} />);
    expect(screen.getByTestId("fader-fill").style.height).toBe("25%");
  });

  it("shows the formatted readout", () => {
    render(<Fader value={0.5} onChange={() => {}} format={(v) => `${(v * 12 - 6).toFixed(1)} dB`} />);
    expect(screen.getByTestId("fader-readout")).toHaveTextContent("0.0 dB");
  });

  it("is inert when disabled", () => {
    const onChange = vi.fn();
    render(<Fader value={0.5} onChange={onChange} disabled />);
    fireEvent.keyDown(screen.getByRole("slider"), { key: "End" });
    expect(onChange).not.toHaveBeenCalled();
  });

  it("marks the root as dragging while the pointer is down", () => {
    render(<Fader value={0.5} onChange={() => {}} />);
    const slider = screen.getByRole("slider");
    const root = slider.parentElement as HTMLElement;
    fireEvent.pointerDown(slider, { clientY: 0, clientX: 0, button: 0, pointerId: 1 });
    expect(root).toHaveAttribute("data-dragging", "true");
    fireEvent.pointerUp(slider, { clientY: 0, clientX: 0, pointerId: 1 });
    expect(root).toHaveAttribute("data-dragging", "false");
  });
});
