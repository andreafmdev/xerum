import { describe, expect, it, vi } from "vitest";
import { render, screen } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { SectionHeader } from "./SectionHeader";

describe("SectionHeader", () => {
  it("renders a decorative LED without onToggle", () => {
    render(<SectionHeader title="Filter" />);
    expect(screen.queryByRole("switch")).not.toBeInTheDocument();
    expect(screen.getByTestId("section-led")).toHaveAttribute("data-on", "true");
  });

  it("turns the LED into a switch named after the title when onToggle is given", async () => {
    const onToggle = vi.fn();
    render(<SectionHeader title="Filter" on={false} onToggle={onToggle} />);
    const led = screen.getByRole("switch", { name: "Filter on" });
    expect(led).toHaveAttribute("aria-checked", "false");
    expect(led).toHaveAttribute("data-on", "false");
    await userEvent.click(led);
    expect(onToggle).toHaveBeenCalledWith(true);
  });
});
