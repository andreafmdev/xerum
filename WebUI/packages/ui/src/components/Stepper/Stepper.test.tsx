import { describe, expect, it, vi } from "vitest";
import { render, screen } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { Stepper } from "./Stepper";

describe("Stepper", () => {
  it("is a spinbutton with min/max and formatted text", () => {
    render(<Stepper value={2} onChange={() => {}} min={-3} max={3} label="Octave" unit="OCT" format={(v) => (v > 0 ? `+${v}` : `${v}`)} />);
    const spin = screen.getByRole("spinbutton", { name: "Octave" });
    expect(spin).toHaveAttribute("aria-valuemin", "-3");
    expect(spin).toHaveAttribute("aria-valuemax", "3");
    expect(spin).toHaveAttribute("aria-valuenow", "2");
    expect(spin).toHaveAttribute("aria-valuetext", "+2");
    expect(screen.getByTestId("stepper-value")).toHaveTextContent("+2");
    expect(screen.getByText("OCT")).toBeInTheDocument();
  });

  it("increments and decrements with the buttons", async () => {
    const onChange = vi.fn();
    render(<Stepper value={0} onChange={onChange} min={-3} max={3} label="Octave" />);
    await userEvent.click(screen.getByRole("button", { name: "Increase Octave" }));
    expect(onChange).toHaveBeenCalledWith(1);
    await userEvent.click(screen.getByRole("button", { name: "Decrease Octave" }));
    expect(onChange).toHaveBeenCalledWith(-1);
  });

  it("clamps at the bounds and disables the edge button", async () => {
    const onChange = vi.fn();
    render(<Stepper value={3} onChange={onChange} min={-3} max={3} label="Octave" />);
    const inc = screen.getByRole("button", { name: "Increase Octave" });
    expect(inc).toBeDisabled();
    await userEvent.click(inc);
    expect(onChange).not.toHaveBeenCalled();
  });

  it("steps with arrow keys, Home and End on the spinbutton", async () => {
    const onChange = vi.fn();
    render(<Stepper value={0} onChange={onChange} min={-3} max={3} label="Octave" />);
    screen.getByRole("spinbutton").focus();
    await userEvent.keyboard("{ArrowUp}");
    expect(onChange).toHaveBeenLastCalledWith(1);
    await userEvent.keyboard("{ArrowDown}");
    expect(onChange).toHaveBeenLastCalledWith(-1);
    await userEvent.keyboard("{End}");
    expect(onChange).toHaveBeenLastCalledWith(3);
    await userEvent.keyboard("{Home}");
    expect(onChange).toHaveBeenLastCalledWith(-3);
  });
});
