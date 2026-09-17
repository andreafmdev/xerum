import { describe, expect, it, vi } from "vitest";
import { render, screen } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { Toggle } from "./Toggle";

describe("Toggle", () => {
  it("is a switch labelled by its label", () => {
    render(<Toggle checked={false} onChange={() => {}} label="Sync" />);
    expect(screen.getByRole("switch", { name: "Sync" })).toHaveAttribute("aria-checked", "false");
  });

  it("calls onChange with the next state on click", async () => {
    const onChange = vi.fn();
    render(<Toggle checked={false} onChange={onChange} label="Sync" />);
    await userEvent.click(screen.getByRole("switch"));
    expect(onChange).toHaveBeenCalledWith(true);
  });

  it("toggles with Space", async () => {
    const onChange = vi.fn();
    render(<Toggle checked={true} onChange={onChange} label="Sync" />);
    screen.getByRole("switch").focus();
    await userEvent.keyboard(" ");
    expect(onChange).toHaveBeenCalledWith(false);
  });

  it("is disabled", async () => {
    const onChange = vi.fn();
    render(<Toggle checked={false} onChange={onChange} label="Sync" disabled />);
    await userEvent.click(screen.getByRole("switch"));
    expect(onChange).not.toHaveBeenCalled();
  });

  it("sets --tone", () => {
    render(<Toggle checked onChange={() => {}} label="Sync" tone="lfo" />);
    expect(screen.getByTestId("toggle").style.getPropertyValue("--tone")).toBe("var(--color-lfo)");
  });
});
