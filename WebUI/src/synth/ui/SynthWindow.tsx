import { useCallback, useEffect, useMemo, useRef, useState } from "react";
import { useBoolParam, useBridgeState, useChoiceParam, useFloatParam } from "../../juce/hooks";
import type { ModSource } from "../../juce/backend";
import type { ParamId } from "../params.generated";
import { useSynth, type TabId } from "../useSynth";
import { Footer } from "./Footer";
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
      L'host JUCE passa 0: lo chassis riempie la WebView e la tastiera nativa
      si attacca senza stacco sotto il bordo inferiore. */
  gutter?: number;
};

const W = 900;
const H = 600;

// `zoom` e non `transform: scale()`: transform rasterizza il sottoalbero alla dimensione di
// layout (900×600) e poi stira il bitmap, quindi sopra 1× testo, bordi da 1 px e ombre
// escono sfocati — proprio il caso in cui l'host JUCE apre la finestra grande (fino a 1.5×,
// vedi kMaxScale in PluginEditor.cpp). `zoom` rifa' il **layout**: tutto viene ridisegnato
// alla risoluzione finale e resta nitido a qualunque scala. In cambio lo chassis occupa
// spazio reale (transform non lo faceva), cosa che qui va bene: `.sx-root` lo centra con
// flex e il fit sotto calcola comunque la scala a partire dal contenitore, non da lui.
// L'unica superficie che `zoom` non puo' salvare da sola e' il <canvas> di WaveDisplay:
// il suo backing store va moltiplicato per la scala a mano (vedi WaveDisplay.tsx).

/** Finestra del plugin: 900×600 scalata per stare nel contenitore. Va montata dentro <BridgeProvider>. */
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
      <div data-testid="chassis" className="sx-chassis" data-variant={variant} data-attached={gutter === 0 ? "" : undefined} style={{ zoom: sc, opacity: bypass.checked ? 0.9 : 1 }}>
        <SynthContext.Provider value={ctx}>
          <MetersProvider>
            <Header
              preset={s.preset}
              dirty={s.dirty}
              onBrowse={() => s.setBrowse(true)}
              onPrev={() => s.stepPreset(-1)}
              onNext={() => s.stepPreset(1)}
            />
            <WaveDisplay position={wtpos.value} warp={warp.value} level={level.value} name={wt.options.find((o) => o.value === wt.value)?.label ?? ""} />
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
          </MetersProvider>
        </SynthContext.Provider>
      </div>
    </div>
  );
}
