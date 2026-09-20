import { useCallback, useEffect, useMemo, useRef, useState } from "react";
import { useBoolParam, useBridgeState, useChoiceParam, useFloatParam } from "../../juce/hooks";
import type { ModSource } from "../../juce/backend";
import type { ParamId } from "../params.generated";
import { useSynth, type TabId } from "../useSynth";
import { BottomStrip } from "./BottomStrip";
import { Header } from "./Header";
import { MetersProvider } from "./MetersContext";
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
  /** Margine totale (px) lasciato attorno allo chassis dall'adattamento.
      L'host JUCE passa 0: lo chassis riempie la WebView senza margine. */
  gutter?: number;
};

const W = 900;
const H = 708;

// Lo chassis si scala con `transform`, non con `zoom`.
//
// `zoom` e' stato provato proprio per risolvere la sfocatura del testo sopra 1x (transform
// rasterizza il sottoalbero alla dimensione di layout e poi stira il bitmap) ed e' stato
// tolto: WebKit — il motore della WKWebView in cui gira davvero il plugin, non Chromium —
// non lo implementa come Chromium. Misurato con Playwright/WebKit a viewport 1309x873 e
// `zoom: 1.4544` sullo chassis: il suo getBoundingClientRect resta 900x600 (non scalato) e i
// discendenti vengono *divisi* per il fattore invece che moltiplicati (il display d'onda,
// 130 px di layout, ne misurava 89.4 = 130 / 1.4544). Nel plugin l'effetto era una fascia
// vuota di ~270 px fra il pannello e la tastiera. Se un giorno si volesse riprovare, serve
// prima una misura su WebKit, non su Chrome.
//
// Il <canvas> di WaveDisplay ha comunque bisogno di conoscere la scala: `transform` non
// tocca il backing store, quindi lo schermo dell'onda restava a risoluzione 1x anche quando
// tutto il resto era ingrandito. Lo ricava da getBoundingClientRect (vedi WaveDisplay.tsx).
/** Finestra del plugin: 900×708 scalata per stare nel contenitore. Va montata dentro <BridgeProvider>. */
export function SynthWindow({ variant = "deep", initialTab = "env", scale: fixedScale, gutter = 16 }: SynthWindowProps) {
  const s = useSynth(initialTab);
  // Unica istanza dello stato condiviso: i tab lo leggono dal contesto, così non
  // esistono copie che si aggiornano a turno con gli echo dell'host.
  const state = useBridgeState();
  const bypass = useBoolParam("bypass");
  const wtpos = useFloatParam("wtpos");
  const warp = useFloatParam("warp");
  const level = useFloatParam("level");
  const wt = useChoiceParam("wtIndex");

  // useBridgeState ricrea addMod/setDepth/removeMod a ogni render: le avvolgiamo
  // dietro un ref per esporre callback stabili e non ricalcolare il contesto.
  const stateRef = useRef(state);
  stateRef.current = state;
  const { setTab, markDirty } = s;
  const addMod = useCallback(
    (src: ModSource, target: ParamId) => {
      stateRef.current.addMod(src, target);
      setTab("mod");
      markDirty();
    },
    [setTab, markDirty],
  );
  const setDepth = useCallback((i: number, depth: number) => { stateRef.current.setDepth(i, depth); markDirty(); }, [markDirty]);
  const removeMod = useCallback((i: number) => { stateRef.current.removeMod(i); markDirty(); }, [markDirty]);
  const setArpSteps = useCallback((steps: number[]) => { stateRef.current.setArpSteps(steps); markDirty(); }, [markDirty]);
  const ctx = useMemo<SynthCtx>(
    () => ({ mods: state.mods, arpSteps: state.arpSteps, addMod, setDepth, removeMod, setArpSteps, markDirty }),
    [state.mods, state.arpSteps, addMod, setDepth, removeMod, setArpSteps, markDirty],
  );

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
      setSc(Math.min((r.width - gutter) / W, (r.height - gutter) / H, 1.5));
    };
    fit();
    const ro = new ResizeObserver(fit);
    ro.observe(el);
    return () => ro.disconnect();
  }, [fixedScale, gutter]);

  return (
    <div className="sx-root" ref={rootRef}>
      <div data-testid="chassis" className="sx-chassis" data-variant={variant} data-attached={gutter === 0 ? "" : undefined} style={{ transform: `scale(${sc})`, opacity: bypass.checked ? 0.9 : 1 }}>
        <SynthContext.Provider value={ctx}>
          <MetersProvider>
            <Header
              preset={s.preset}
              dirty={s.dirty}
              onBrowse={() => s.setBrowse(true)}
              onPrev={() => s.stepPreset(-1)}
              onNext={() => s.stepPreset(1)}
            />
            <WaveDisplay position={wtpos.value} warp={warp.value} level={level.value} scale={sc} name={wt.options.find((o) => o.value === wt.value)?.label ?? ""} />
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
            <BottomStrip />
            {s.browse && <PresetOverlay current={s.preset} onPick={s.pick} onClose={() => s.setBrowse(false)} />}
          </MetersProvider>
        </SynthContext.Provider>
      </div>
    </div>
  );
}
