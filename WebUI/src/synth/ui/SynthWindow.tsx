import { useCallback, useEffect, useMemo, useRef, useState } from "react";
import { useBoolParam, useBridgeState, useChoiceParam, useFloatParam, useMeters } from "../../juce/hooks";
import type { ModSource } from "../../juce/backend";
import { modsFor, type SourceLevels } from "../mod";
import type { ParamId } from "../params.generated";
import { useSynth, type TabId } from "../useSynth";
import { Footer } from "./Footer";
import { Header } from "./Header";
import { FilterPanel, MasterPanel, OscPanel } from "./Panels";
import { PresetOverlay } from "./PresetOverlay";
import { SynthContext, type SynthCtx } from "./SynthContext";
import { ArpTab, EnvTab, FxTab, LfoTab, ModTab, TabArea } from "./Tabs";
import { WaveDisplay } from "./WaveDisplay";
import "./synth.css";

export type SynthVariant = "deep" | "soft" | "glow";

export type SynthWindowProps = {
  /** Materiale del pannello. */
  variant?: SynthVariant;
  initialTab?: TabId;
  /** Scala fissa invece dell'adattamento al contenitore. */
  scale?: number;
};

const W = 900;
const H = 600;

/** Finestra del plugin: 900×600 scalata per stare nel contenitore. Va montata dentro <BridgeProvider>. */
export function SynthWindow({ variant = "deep", initialTab = "env", scale: fixedScale }: SynthWindowProps) {
  const s = useSynth(initialTab);
  const state = useBridgeState();
  const meters = useMeters();
  const bypass = useBoolParam("bypass");
  const wtpos = useFloatParam("wtpos");
  const warp = useFloatParam("warp");
  const level = useFloatParam("level");
  const wt = useChoiceParam("wtIndex");

  // L'host manda solo il livello dell'LFO: le altre sorgenti restano valori di
  // comodo finché il C++ non le espone.
  const sources = useMemo<SourceLevels>(() => ({ lfo: meters.lfo, env: 0.6, vel: 0.7, mw: 0.5 }), [meters.lfo]);

  // addMod cambia identità a ogni render di useBridgeState: lo teniamo in un ref
  // così il contesto si ricalcola solo quando cambia davvero qualcosa.
  const addModRef = useRef(state.addMod);
  addModRef.current = state.addMod;
  const setTab = s.setTab;
  const addMod = useCallback(
    (src: ModSource, target: ParamId) => {
      addModRef.current(src, target);
      setTab("mod");
    },
    [setTab],
  );
  const ctx = useMemo<SynthCtx>(
    () => ({ mods: state.mods, addMod, sources, markDirty: s.markDirty }),
    [state.mods, addMod, sources, s.markDirty],
  );

  const posLive = modsFor(state.mods, "wtpos").reduce((a, m) => a + (m.src === "lfo" ? m.depth * meters.lfo : 0), 0);

  const rootRef = useRef<HTMLDivElement>(null);
  const [sc, setSc] = useState(fixedScale ?? 1);
  useEffect(() => {
    if (fixedScale) {
      setSc(fixedScale);
      return;
    }
    const el = rootRef.current;
    if (!el) return;
    const fit = () => {
      const r = el.getBoundingClientRect();
      if (!r.width || !r.height) return;
      setSc(Math.min((r.width - 16) / W, (r.height - 16) / H, 1.5));
    };
    fit();
    const ro = new ResizeObserver(fit);
    ro.observe(el);
    return () => ro.disconnect();
  }, [fixedScale]);

  return (
    <div className="sx-root" ref={rootRef}>
      <div data-testid="chassis" className="sx-chassis" data-variant={variant} style={{ transform: `scale(${sc})`, opacity: bypass.checked ? 0.9 : 1 }}>
        <SynthContext.Provider value={ctx}>
          <Header
            preset={s.preset}
            dirty={s.dirty}
            onBrowse={() => s.setBrowse(true)}
            onPrev={() => s.stepPreset(-1)}
            onNext={() => s.stepPreset(1)}
          />
          <WaveDisplay position={wtpos.value} warp={warp.value} level={level.value} lfo={posLive} name={wt.options.find((o) => o.value === wt.value)?.label ?? ""} />
          <div className="flex h-56 shrink-0 gap-2">
            <OscPanel />
            <FilterPanel />
            <MasterPanel />
          </div>
          <TabArea tab={s.tab} setTab={s.setTab}>
            {s.tab === "env" && <EnvTab />}
            {s.tab === "lfo" && <LfoTab />}
            {s.tab === "mod" && <ModTab />}
            {s.tab === "fx" && <FxTab />}
            {s.tab === "arp" && <ArpTab />}
          </TabArea>
          <Footer />
          {s.browse && <PresetOverlay current={s.preset} onPick={s.pick} onClose={() => s.setBrowse(false)} />}
        </SynthContext.Provider>
      </div>
    </div>
  );
}
