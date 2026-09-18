import { useCallback, useState } from "react";
import { PRESETS, step, type Preset } from "./presets";

export type TabId = "env" | "lfo" | "mod" | "fx" | "arp";

/**
 * Stato della sola finestra: tab aperto, preset selezionato, browser dei preset
 * e il flag "modificato". Parametri, mod matrix e arp vivono nel bridge.
 */
export function useSynth(initialTab: TabId) {
  const [tab, setTab] = useState<TabId>(initialTab);
  const [preset, setPreset] = useState<Preset>(PRESETS[1]!);
  const [browse, setBrowse] = useState(false);
  const [dirty, setDirty] = useState(false);

  /** Prima scrittura di un parametro dopo un preset: il nome mostra l'asterisco. */
  const markDirty = useCallback(() => setDirty(true), []);

  const pick = useCallback((pr: Preset) => {
    setPreset(pr);
    setDirty(false);
    setBrowse(false);
  }, []);
  const stepPreset = useCallback(
    (delta: number) => {
      const i = PRESETS.findIndex((x) => x.name === preset.name);
      pick(PRESETS[step(i, delta, PRESETS.length)]!);
    },
    [pick, preset.name],
  );

  return { tab, setTab, preset, pick, stepPreset, browse, setBrowse, dirty, markDirty };
}
