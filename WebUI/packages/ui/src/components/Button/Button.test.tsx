import { describe, expect, it, vi } from "vitest";
import { render, screen } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { Button } from "./Button";

describe("Button", () => {
  it("renders a button and fires onClick", async () => {
    const onClick = vi.fn();
    render(<Button onClick={onClick}>Init</Button>);
    await userEvent.click(screen.getByRole("button", { name: "Init" }));
    expect(onClick).toHaveBeenCalledTimes(1);
  });

  it("applies the tone as --tone and uses it for the default variant", () => {
    render(<Button tone="env">Env</Button>);
    const b = screen.getByRole("button");
    expect(b.style.getPropertyValue("--tone")).toBe("var(--color-env)");
    expect(b.className).toContain("bg-(--tone)");
  });

  it("does not recolor non-default variants", () => {
    render(<Button tone="env" variant="ghost">Env</Button>);
    expect(screen.getByRole("button").className).not.toContain("bg-(--tone)");
  });

  it("supports disabled", () => {
    render(<Button disabled>Off</Button>);
    expect(screen.getByRole("button")).toBeDisabled();
  });
});
