import { describe, expect, it, vi } from "vitest";
import { render, screen, within } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { PRESETS } from "../presets";
import { PresetOverlay } from "./PresetOverlay";

const init = PRESETS.find((p) => p.name === "Init")!;

function mount() {
  const onPick = vi.fn();
  const onClose = vi.fn();
  render(<PresetOverlay current={init} onPick={onPick} onClose={onClose} />);
  return { onPick, onClose, dialog: screen.getByRole("dialog", { name: "Presets" }) };
}

describe("PresetOverlay", () => {
  it("opens on the current preset, with its bank and author in the preview", () => {
    const { dialog } = mount();
    expect(within(dialog).getByTestId("preset-preview-name")).toHaveTextContent("Init");
    expect(within(dialog).getByText("Factory")).toBeInTheDocument();
    expect(within(dialog).getByRole("button", { name: "Caricato" })).toBeInTheDocument();
  });

  it("a click only selects: the preview follows, nothing is loaded", async () => {
    const { dialog, onPick } = mount();
    await userEvent.click(within(dialog).getByRole("button", { name: /Glass Pad/ }));
    expect(within(dialog).getByTestId("preset-preview-name")).toHaveTextContent("Glass Pad");
    expect(within(dialog).getByRole("button", { name: /Glass Pad/ })).toHaveAttribute("aria-pressed", "true");
    expect(onPick).not.toHaveBeenCalled();
  });

  it("the load button picks the selected preset", async () => {
    const { dialog, onPick } = mount();
    await userEvent.click(within(dialog).getByRole("button", { name: /Glass Pad/ }));
    await userEvent.click(within(dialog).getByRole("button", { name: "Carica preset" }));
    expect(onPick).toHaveBeenCalledWith(expect.objectContaining({ name: "Glass Pad" }));
  });

  it("a double click on a card picks it straight away", async () => {
    const { dialog, onPick } = mount();
    await userEvent.dblClick(within(dialog).getByRole("button", { name: /Acid Line/ }));
    expect(onPick).toHaveBeenCalledWith(expect.objectContaining({ name: "Acid Line" }));
  });

  it("category and search narrow the grid", async () => {
    const { dialog } = mount();
    await userEvent.click(within(dialog).getByRole("button", { name: /^Bass/ }));
    expect(within(dialog).queryByRole("button", { name: /Glass Pad/ })).not.toBeInTheDocument();
    expect(within(dialog).getByRole("button", { name: /Sub Pulse/ })).toBeInTheDocument();
    await userEvent.type(within(dialog).getByRole("searchbox"), "acid");
    expect(within(dialog).queryByRole("button", { name: /Sub Pulse/ })).not.toBeInTheDocument();
    expect(within(dialog).getByRole("button", { name: /Acid Line/ })).toBeInTheDocument();
  });

  it("Escape closes", async () => {
    const { dialog, onClose } = mount();
    await userEvent.type(within(dialog).getByRole("searchbox"), "{Escape}");
    expect(onClose).toHaveBeenCalled();
  });
});
