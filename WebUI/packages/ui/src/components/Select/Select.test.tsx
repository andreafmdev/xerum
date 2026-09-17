import { describe, expect, it, vi } from "vitest";
import { render, screen } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { Select } from "./Select";

const options = [
  { value: "saw", label: "Saw" },
  { value: "square", label: "Square" },
  { value: "sine", label: "Sine" },
];

describe("Select", () => {
  it("renders the selected label in the trigger", () => {
    render(<Select value="square" onChange={() => {}} options={options} label="Waveform" />);
    expect(screen.getByRole("combobox", { name: "Waveform" })).toHaveTextContent("Square");
  });

  it("shows the placeholder when nothing is selected", () => {
    render(<Select value={null} onChange={() => {}} options={options} placeholder="Pick a wave" />);
    expect(screen.getByRole("combobox")).toHaveTextContent("Pick a wave");
  });

  it("opens and selects an option", async () => {
    const onChange = vi.fn();
    render(<Select value="saw" onChange={onChange} options={options} label="Waveform" />);
    await userEvent.click(screen.getByRole("combobox"));
    await userEvent.click(await screen.findByRole("option", { name: "Sine" }));
    expect(onChange).toHaveBeenCalledWith("sine");
  });

  it("is disabled", () => {
    render(<Select value="saw" onChange={() => {}} options={options} disabled />);
    expect(screen.getByRole("combobox")).toBeDisabled();
  });
});
