import { useCallback, useState } from "react";
import { useBackend } from "../juce/provider";
import { PRESETS, step, type Preset } from "./presets";

export type TabId = "env" | "lfo" | "mod" | "fx" | "arp";

// "Init" e' il patch che corrisponde ai default APVTS: e' solo l'etichetta di
// partenza, non un caricamento — vedi il commento su pick qui sotto.
const INITIAL_PRESET: Preset = PRESETS.find((p) => p.name === "Init") ?? PRESETS[0]!;

/**
 * Stato della sola finestra: tab aperto, preset selezionato, browser dei preset
 * e il flag "modificato". Parametri, mod matrix e arp vivono nel bridge.
 */
export function useSynth(initialTab: TabId) {
  const backend = useBackend();
  const [tab, setTab] = useState<TabId>(initialTab);
  // Solo l'etichetta iniziale: NON chiamiamo backend.loadPreset qui. Al mount
  // l'host ha gia' ripristinato lo stato del progetto; caricare un preset ora
  // lo sovrascriverebbe.
  const [preset, setPreset] = useState<Preset>(INITIAL_PRESET);
  const [browse, setBrowse] = useState(false);
  const [settings, setSettings] = useState(false);
  const [dirty, setDirty] = useState(false);

  /** Prima scrittura di un parametro dopo un preset: il nome mostra l'asterisco. */
  const markDirty = useCallback(() => setDirty(true), []);

  // Solo una scelta esplicita dell'utente arriva qui: e' l'unico punto che
  // chiama backend.loadPreset.
  const pick = useCallback(
    (pr: Preset) => {
      setPreset(pr);
      setDirty(false);
      setBrowse(false);
      void backend.loadPreset(PRESETS.indexOf(pr)).catch((e) => console.error("[preset] load fallito", e));
    },
    [backend],
  );
  const stepPreset = useCallback(
    (delta: number) => {
      const i = PRESETS.findIndex((x) => x.name === preset.name);
      pick(PRESETS[step(i, delta, PRESETS.length)]!);
    },
    [pick, preset.name],
  );

  return { tab, setTab, preset, pick, stepPreset, browse, setBrowse, settings, setSettings, dirty, markDirty };
}
