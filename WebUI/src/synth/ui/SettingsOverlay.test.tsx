import { describe, expect, it, vi } from "vitest";
import { act, render, screen } from "@testing-library/react";
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

  // I2 della review finale: onKeyDown sta sul div del dialog e scatta solo col focus al suo
  // interno, ma aprire il pannello non mette focus su niente da solo — a differenza di
  // PresetOverlay, che ce l'ha gratis dall'autoFocus sul campo di ricerca. Senza mettere a
  // fuoco il contenitore al mount, questi due test sarebbero rossi (Escape lettera morta).
  it("Escape chiude il pannello", async () => {
    const onClose = vi.fn();
    render(
      <BridgeProvider backend={new FakeBackend({ audioSettings: standalone })}>
        <SettingsOverlay onClose={onClose} />
      </BridgeProvider>,
    );
    await screen.findByLabelText("Audio output");
    await userEvent.keyboard("{Escape}");
    expect(onClose).toHaveBeenCalled();
  });

  it("con la conferma di ripristino aperta, Escape chiude solo quella", async () => {
    const onClose = vi.fn();
    render(
      <BridgeProvider backend={new FakeBackend({ audioSettings: standalone })}>
        <SettingsOverlay onClose={onClose} />
      </BridgeProvider>,
    );
    await userEvent.click(await screen.findByRole("button", { name: "Ripristina i valori di fabbrica" }));
    expect(screen.getByRole("alertdialog")).toBeInTheDocument();

    await userEvent.keyboard("{Escape}");
    expect(screen.queryByRole("alertdialog")).not.toBeInTheDocument();
    expect(onClose).not.toHaveBeenCalled();

    await userEvent.keyboard("{Escape}");
    expect(onClose).toHaveBeenCalled();
  });

  it("porta dentro anche il selettore MIDI", async () => {
    renderWith(new FakeBackend({ audioSettings: standalone,
      midiInputs: { host: false, devices: [{ id: "k1", name: "Keystation", enabled: true }] } }));
    expect(await screen.findByLabelText("MIDI input")).toBeInTheDocument();
  });

  // Portati da PerformanceBar.test.tsx (git show 2214483~1): il selettore e' traslocato qui,
  // e con lui il comportamento reale di select() — midiLog, l'evento midiInputsChanged, la
  // semantica "un solo device attivo" vs "Tutti gli ingressi" — non solo che il controllo esiste.
  describe("selettore MIDI nello Standalone", () => {
    const midiStandalone = () => new FakeBackend({ midiInputs: { host: false, devices: [
      { id: "id-a", name: "Keystation", enabled: true },
      { id: "id-b", name: "Launchkey", enabled: false },
    ] } });

    it("mostra un selettore con l'ingresso attivo al posto della scritta dell'host", async () => {
      renderWith(midiStandalone());
      await act(async () => {});
      expect(screen.queryByTestId("midi-host")).toBeNull();
      expect(screen.getByRole("combobox", { name: "MIDI input" })).toHaveTextContent("Keystation");
    });

    it("scegliere un ingresso abilita quello e spegne gli altri", async () => {
      const b = midiStandalone();
      renderWith(b);
      await act(async () => {});
      await userEvent.click(screen.getByRole("combobox", { name: "MIDI input" }));
      await userEvent.click(await screen.findByRole("option", { name: "Launchkey" }));
      expect(b.midiLog).toEqual([["id-b", true], ["id-a", false]]);
    });

    it("\"Tutti gli ingressi\" li abilita tutti", async () => {
      const b = midiStandalone();
      renderWith(b);
      await act(async () => {});
      await userEvent.click(screen.getByRole("combobox", { name: "MIDI input" }));
      await userEvent.click(await screen.findByRole("option", { name: "Tutti gli ingressi" }));
      expect(b.midiLog).toEqual([["id-a", true], ["id-b", true]]);
    });

    it("l'evento midiInputsChanged aggiorna lista e selezione", async () => {
      const b = midiStandalone();
      renderWith(b);
      await act(async () => {});
      act(() => b.emitMidiInputsChanged({ host: false, devices: [
        { id: "id-a", name: "Keystation", enabled: false },
        { id: "id-c", name: "nanoKEY", enabled: true },
      ] }));
      expect(screen.getByRole("combobox", { name: "MIDI input" })).toHaveTextContent("nanoKEY");
    });

    it("con piu' ingressi abilitati mostra \"Tutti gli ingressi\"", async () => {
      renderWith(new FakeBackend({ midiInputs: { host: false, devices: [
        { id: "id-a", name: "Keystation", enabled: true },
        { id: "id-b", name: "Launchkey", enabled: true },
      ] } }));
      await act(async () => {});
      expect(screen.getByRole("combobox", { name: "MIDI input" })).toHaveTextContent("Tutti gli ingressi");
    });
  });

  // Il ripristino di fabbrica butta via la patch su cui l'utente sta lavorando: un misclick
  // dentro un pannello di impostazioni non deve poterlo fare da solo, quindi passa sempre
  // da una conferma esplicita (role="alertdialog") prima di raggiungere il backend.
  describe("ripristino ai valori di fabbrica", () => {
    it("il ripristino chiede conferma prima di buttare via la patch", async () => {
      const backend = new FakeBackend({ audioSettings: standalone });
      renderWith(backend);
      await userEvent.click(await screen.findByRole("button", { name: "Ripristina i valori di fabbrica" }));
      expect(backend.resets).toBe(0);
      expect(screen.getByRole("alertdialog")).toBeInTheDocument();
    });

    it("confermando, il ripristino arriva al backend", async () => {
      const backend = new FakeBackend({ audioSettings: standalone });
      renderWith(backend);
      await userEvent.click(await screen.findByRole("button", { name: "Ripristina i valori di fabbrica" }));
      await userEvent.click(screen.getByRole("button", { name: "Ripristina" }));
      expect(backend.resets).toBe(1);
    });

    it("annullando, non succede niente", async () => {
      const backend = new FakeBackend({ audioSettings: standalone });
      renderWith(backend);
      await userEvent.click(await screen.findByRole("button", { name: "Ripristina i valori di fabbrica" }));
      await userEvent.click(screen.getByRole("button", { name: "Annulla" }));
      expect(backend.resets).toBe(0);
      expect(screen.queryByRole("alertdialog")).not.toBeInTheDocument();
    });
  });
});
