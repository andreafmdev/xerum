import { describe, expect, it } from "vitest";
import { render, screen } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { SettingsOverlay } from "./SettingsOverlay";
import { FakeBackend } from "../../juce/fake-backend";
import { BridgeProvider } from "../../juce/provider";

const standalone = {
  standalone: true,
  outputs: [{ id: "Scarlett", name: "Focusrite 2i2" }, { id: "MacBook", name: "MacBook Pro Speakers" }],
  currentOutput: "Scarlett",
  sampleRates: [44100, 48000], currentSampleRate: 48000,
  bufferSizes: [64, 128, 256], currentBufferSize: 128,
  latencyMs: 2.67,
};

const renderWith = (backend: FakeBackend) =>
  render(
    <BridgeProvider backend={backend}>
      <SettingsOverlay onClose={() => {}} />
    </BridgeProvider>,
  );

describe("SettingsOverlay", () => {
  it("nello Standalone mostra i controlli del device audio", async () => {
    renderWith(new FakeBackend({ audioSettings: standalone }));
    expect(await screen.findByLabelText("Audio output")).toBeInTheDocument();
    expect(screen.getByLabelText("Sample rate")).toBeInTheDocument();
    expect(screen.getByLabelText("Buffer size")).toBeInTheDocument();
  });

  it("nel plugin mostra una riga di testo invece dei controlli", async () => {
    renderWith(new FakeBackend());
    expect(await screen.findByTestId("audio-host")).toBeInTheDocument();
    expect(screen.queryByLabelText("Audio output")).not.toBeInTheDocument();
  });

  it("mostra l'errore che la setter restituisce", async () => {
    // Select (@xerum/ui) e' un trigger base-ui (role="combobox"), non una <select> nativa:
    // userEvent.selectOptions non si applica, si passa dal click come in PerformanceBar.test.tsx.
    const backend = new FakeBackend({ audioSettings: standalone });
    backend.failNextAudioChange("Il device non si apre a 44.1 kHz");
    renderWith(backend);
    await userEvent.click(await screen.findByLabelText("Sample rate"));
    await userEvent.click(await screen.findByRole("option", { name: "44100 Hz" }));
    expect(await screen.findByRole("alert")).toHaveTextContent("Il device non si apre a 44.1 kHz");
  });

  it("porta dentro anche il selettore MIDI", async () => {
    renderWith(new FakeBackend({ audioSettings: standalone,
      midiInputs: { host: false, devices: [{ id: "k1", name: "Keystation", enabled: true }] } }));
    expect(await screen.findByLabelText("MIDI input")).toBeInTheDocument();
  });
});
