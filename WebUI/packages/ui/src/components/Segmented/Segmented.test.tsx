import { describe, expect, it, vi } from "vitest";
import { useState } from "react";
import { render, screen } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { Segmented } from "./Segmented";

const options = [
  { value: "lp", label: "LP" },
  { value: "hp", label: "HP" },
  { value: "bp", label: "BP" },
];

describe("Segmented", () => {
  it("is a radiogroup named by label with the current option checked", () => {
    render(<Segmented value="hp" onChange={() => {}} options={options} label="Filter type" />);
    expect(screen.getByRole("radiogroup", { name: "Filter type" })).toBeInTheDocument();
    expect(screen.getByRole("radio", { name: "HP" })).toHaveAttribute("aria-checked", "true");
    expect(screen.getByRole("radio", { name: "LP" })).toHaveAttribute("aria-checked", "false");
  });

  it("calls onChange with the clicked value", async () => {
    const onChange = vi.fn();
    render(<Segmented value="lp" onChange={onChange} options={options} label="Filter type" />);
    await userEvent.click(screen.getByRole("radio", { name: "BP" }));
    expect(onChange).toHaveBeenCalledWith("bp");
  });

  it("moves with arrow keys and wraps", async () => {
    const seen: string[] = [];
    function Controlled() {
      const [v, setV] = useState("bp");
      return <Segmented value={v} onChange={(n) => { seen.push(n); setV(n); }} options={options} label="Filter type" />;
    }
    render(<Controlled />);
    screen.getByRole("radio", { name: "BP" }).focus();
    await userEvent.keyboard("{ArrowRight}");
    expect(screen.getByRole("radio", { name: "LP" })).toHaveAttribute("aria-checked", "true");
    await userEvent.keyboard("{ArrowLeft}{ArrowLeft}");
    expect(seen).toEqual(["lp", "bp", "hp"]);
    expect(screen.getByRole("radio", { name: "HP" })).toHaveFocus();
  });

  it("only the checked option is in the tab order", () => {
    render(<Segmented value="hp" onChange={() => {}} options={options} label="Filter type" />);
    expect(screen.getByRole("radio", { name: "HP" })).toHaveAttribute("tabindex", "0");
    expect(screen.getByRole("radio", { name: "LP" })).toHaveAttribute("tabindex", "-1");
  });

  it("sets --tone and disables", () => {
    render(<Segmented value="hp" onChange={() => {}} options={options} label="Filter type" tone="filter" disabled />);
    expect(screen.getByRole("radiogroup").style.getPropertyValue("--tone")).toBe("var(--color-filter)");
    expect(screen.getByRole("radio", { name: "HP" })).toBeDisabled();
  });
});
