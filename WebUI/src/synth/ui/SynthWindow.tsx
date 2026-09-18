import { useEffect, useRef, useState } from "react";
import { lfoHz, lfoShape, liveValue, modsFor, type SourceLevels } from "../mod";
import { WAVETABLES } from "../presets";
import { useClock } from "../useClock";
import { useSynth, type TabId } from "../useSynth";
import { Footer } from "./Footer";
import { Header } from "./Header";
import { FilterPanel, MasterPanel, OscPanel } from "./Panels";
import { PresetOverlay } from "./PresetOverlay";
import { ArpTab, EnvTab, FxTab, LfoTab, ModTab, TabArea } from "./Tabs";
import { WaveDisplay } from "./WaveDisplay";
import type { SynthKnobCtx } from "./ParamKnob";
import "./synth.css";

export type SynthVariant = "deep" | "soft" | "glow";

export type SynthWindowProps = {
  /** Materiale del pannello. */
  variant?: SynthVariant;
  /** Clock interno (LFO, meter, arp) acceso. */
  animate?: boolean;
  initialTab?: TabId;
  /** Scala fissa invece dell'adattamento al contenitore. */
  scale?: number;
};

const W = 900;
const H = 600;

/** Finestra del plugin: 900×600 scalata per stare nel contenitore. */
export function SynthWindow({ variant = "deep", animate = true, initialTab = "env", scale: fixedScale }: SynthWindowProps) {
  const s = useSynth(initialTab);
  const { p, set, mods, addMod, tab, setTab, preset, pick, stepPreset, browse, setBrowse, bypass, setBypass, dirty } = s;
  const t = useClock(animate);

  const phase = (t * lfoHz(p.lrate, p.lsync) + p.lphase) % 1;
  const lfo = lfoShape(p.lshape, phase);
  const env = animate ? 0.55 + 0.25 * Math.sin(t * 1.7) + 0.1 * Math.sin(t * 7.3) : 0.6;
  const sources: SourceLevels = { lfo, env, vel: 0.7, mw: 0.5 };
  const ctx: SynthKnobCtx = { p, set, mods, sources, addMod };

  const cutLive = liveValue(p.cutoff, modsFor(mods, "cutoff"), sources);
  const posLive = modsFor(mods, "wtpos").reduce((a, m) => a + (m.src === "lfo" ? m.depth * lfo : 0), 0);
  const arpStep = Math.floor(t * 8) % 16;
  const inL = bypass ? 0 : env * p.level;
  const outL = bypass ? env * 0.5 : env * p.volume * (p.filtOn ? 0.8 + 0.2 * cutLive : 1);

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
      <div data-testid="chassis" className="sx-chassis" data-variant={variant} style={{ transform: `scale(${sc})`, opacity: bypass ? 0.9 : 1 }}>
        <Header
          preset={preset}
          dirty={dirty}
          bypass={bypass}
          onBypass={setBypass}
          onBrowse={() => setBrowse(true)}
          onPrev={() => stepPreset(-1)}
          onNext={() => stepPreset(1)}
        />
        <WaveDisplay position={p.wtpos} warp={p.warp} level={p.level} lfo={posLive} name={WAVETABLES[p.wtIndex]!} />
        <div className="flex h-56 shrink-0 gap-2">
          <OscPanel ctx={ctx} />
          <FilterPanel ctx={ctx} cutLive={cutLive} />
          <MasterPanel ctx={ctx} />
        </div>
        <TabArea tab={tab} setTab={setTab}>
          {tab === "env" && <EnvTab ctx={ctx} />}
          {tab === "lfo" && <LfoTab ctx={ctx} phase={phase} />}
          {tab === "mod" && <ModTab mods={mods} setDepth={s.setDepth} remove={s.removeMod} />}
          {tab === "fx" && <FxTab ctx={ctx} />}
          {tab === "arp" && <ArpTab ctx={ctx} step={arpStep} />}
        </TabArea>
        <Footer inL={inL} outL={outL} voices={p.arpOn ? 1 : p.voiceMode === "Poly" ? 4 : 1} cpu={String(3 + (p.fx2On ? 2 : 0) + p.unison)} />
        {browse && <PresetOverlay current={preset} onPick={pick} onClose={() => setBrowse(false)} />}
      </div>
    </div>
  );
}
