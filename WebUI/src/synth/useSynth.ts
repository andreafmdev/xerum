import { useCallback, useState } from "react";
import { addMod as addModPure, type ModAssignment, type ModSource } from "./mod";
import { DEFAULTS, type KnobId, type SynthParams } from "./params";
import { PRESETS, step, type Preset } from "./presets";

export type TabId = "env" | "lfo" | "mod" | "fx" | "arp";

export const INITIAL_MODS: ModAssignment[] = [
  { src: "lfo", target: "cutoff", depth: 0.25 },
  { src: "env", target: "wtpos", depth: 0.3 },
];

/** Stato della finestra: parametri, mod matrix, preset, tab, bypass. */
export function useSynth(initialTab: TabId) {
  const [p, setP] = useState<SynthParams>(DEFAULTS);
  const [mods, setMods] = useState<ModAssignment[]>(INITIAL_MODS);
  const [tab, setTab] = useState<TabId>(initialTab);
  const [preset, setPreset] = useState<Preset>(PRESETS[1]!);
  const [browse, setBrowse] = useState(false);
  const [bypass, setBypass] = useState(false);
  const [dirty, setDirty] = useState(false);

  const set = useCallback(<K extends keyof SynthParams>(id: K, v: SynthParams[K]) => {
    setP((s) => ({ ...s, [id]: v }));
    setDirty(true);
  }, []);

  const addMod = useCallback((src: ModSource, target: KnobId) => {
    setMods((ms) => addModPure(ms, src, target));
    setTab("mod");
  }, []);
  const setDepth = useCallback((i: number, depth: number) => {
    setMods((ms) => ms.map((m, j) => (j === i ? { ...m, depth } : m)));
  }, []);
  const removeMod = useCallback((i: number) => setMods((ms) => ms.filter((_, j) => j !== i)), []);

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

  return { p, set, mods, addMod, setDepth, removeMod, tab, setTab, preset, pick, stepPreset, browse, setBrowse, bypass, setBypass, dirty };
}
