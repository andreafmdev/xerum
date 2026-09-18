import { useMemo } from "react";
import { Panel, Segmented, Stepper } from "@xerum/ui";
import { ChevronLeft, ChevronRight } from "lucide-react";
import { filterPath } from "../curves";
import { fmt } from "../format";
import type { FilterType, VoiceMode } from "../params";
import { step, WAVETABLES } from "../presets";
import { ParamKnob, type SynthKnobCtx } from "./ParamKnob";

const plate = "sx-plate min-w-0 gap-1.5 pt-1.5";
const body = "flex flex-1 items-end justify-between gap-1 px-2.5 pb-2.5";

const UNISON = [{ value: "0", label: "1" }, { value: "1", label: "2" }, { value: "2", label: "4" }, { value: "3", label: "8" }];

export function OscPanel({ ctx }: { ctx: SynthKnobCtx }) {
  const { p, set } = ctx;
  return (
    <Panel
      title="Oscillator"
      tone="osc"
      className={`${plate} flex-[0_0_430px]`}
      on={p.oscOn}
      onToggle={(v) => set("oscOn", v)}
      actions={
        <>
          <Segmented label="Unison voices" value={String(p.unison)} onChange={(v) => set("unison", Number(v))} options={UNISON} />
          <span className="text-2xs tracking-wider text-text-dim">UNISON</span>
        </>
      }
    >
      <div className={body}>
        <div className="flex h-full w-32 shrink-0 flex-col justify-between gap-1.5 self-stretch">
          <div className="flex h-6.5 items-center gap-1 rounded-control bg-well px-1.5 shadow-well">
            <button type="button" aria-label="Previous wavetable" onClick={() => set("wtIndex", step(p.wtIndex, -1, WAVETABLES.length))} className="text-text-dim hover:text-foreground">
              <ChevronLeft className="size-3.5" />
            </button>
            <span className="flex-1 truncate text-center font-mono text-2xs">{WAVETABLES[p.wtIndex]}</span>
            <button type="button" aria-label="Next wavetable" onClick={() => set("wtIndex", step(p.wtIndex, 1, WAVETABLES.length))} className="text-text-dim hover:text-foreground">
              <ChevronRight className="size-3.5" />
            </button>
          </div>
          <div className="flex flex-col gap-1.5">
            <Stepper value={p.oct} onChange={(v) => set("oct", v)} min={-3} max={3} unit="OCT" label="Octave" format={fmt.signedInt} />
            <Stepper value={p.semi} onChange={(v) => set("semi", v)} min={-12} max={12} unit="SEMI" label="Semitones" format={fmt.signedInt} />
          </div>
        </div>
        <ParamKnob id="wtpos" ctx={ctx} label="Position" size="lg" format={fmt.frame} />
        <ParamKnob id="warp" ctx={ctx} label="Warp" format={fmt.pct} />
        <ParamKnob id="detune" ctx={ctx} label="Detune" format={fmt.cents} />
        <ParamKnob id="fine" ctx={ctx} label="Fine" bipolar format={fmt.fine} />
        <ParamKnob id="level" ctx={ctx} label="Level" format={fmt.db} />
      </div>
    </Panel>
  );
}

const FTYPES: { value: FilterType; label: string }[] = [{ value: "LP", label: "LP" }, { value: "HP", label: "HP" }, { value: "BP", label: "BP" }];
const SLOPES = [{ value: "12", label: "12" }, { value: "24", label: "24" }];

export function FilterPanel({ ctx, cutLive }: { ctx: SynthKnobCtx; cutLive: number }) {
  const { p, set } = ctx;
  const W = 236;
  const H = 58;
  const d = useMemo(() => filterPath(cutLive, p.res, p.ftype, W, H), [cutLive, p.res, p.ftype]);
  return (
    <Panel
      title="Filter"
      tone="filter"
      className={`${plate} flex-[1_1_0]`}
      on={p.filtOn}
      onToggle={(v) => set("filtOn", v)}
      actions={
        <>
          <Segmented label="Filter type" value={p.ftype} onChange={(v) => set("ftype", v)} options={FTYPES} />
          <Segmented label="Slope" value={String(p.slope)} onChange={(v) => set("slope", Number(v) as 12 | 24)} options={SLOPES} />
        </>
      }
    >
      <div className="mx-2.5 overflow-hidden rounded-control bg-well shadow-well" style={{ height: H }}>
        <svg width="100%" height={H} viewBox={`0 0 ${W} ${H}`} preserveAspectRatio="none" className="block">
          {[0.25, 0.5, 0.75].map((x) => (
            <line key={x} x1={x * W} x2={x * W} y1="0" y2={H} className="stroke-line-strong" opacity={0.3} />
          ))}
          <path d={`${d} L${W} ${H} L0 ${H} Z`} className="fill-(--tone)" opacity={0.12} />
          <path d={d} className="fill-none stroke-(--tone) [filter:var(--glow)]" strokeWidth={1.8} />
        </svg>
      </div>
      <div className={`${body} justify-around`}>
        <ParamKnob id="cutoff" ctx={ctx} label="Cutoff" size="lg" format={fmt.hz} disabled={!p.filtOn} />
        <ParamKnob id="res" ctx={ctx} label="Resonance" format={fmt.pct} disabled={!p.filtOn} />
        <ParamKnob id="drive" ctx={ctx} label="Drive" format={fmt.drive} disabled={!p.filtOn} />
        <ParamKnob id="keytrk" ctx={ctx} label="Key trk" format={fmt.pct} disabled={!p.filtOn} />
      </div>
    </Panel>
  );
}

const VOICES: { value: VoiceMode; label: string }[] = [{ value: "Poly", label: "Poly" }, { value: "Mono", label: "Mono" }, { value: "Legato", label: "Legato" }];

export function MasterPanel({ ctx }: { ctx: SynthKnobCtx }) {
  const { p, set } = ctx;
  return (
    <Panel title="Master" tone="master" className={`${plate} flex-[0_0_176px]`}>
      <div className="px-2.5">
        <Segmented label="Voice mode" value={p.voiceMode} onChange={(v) => set("voiceMode", v)} options={VOICES} />
      </div>
      <div className={`${body} justify-around`}>
        <div className="flex gap-2">
          <ParamKnob id="pan" ctx={ctx} label="Pan" bipolar size="sm" format={fmt.pan} />
          <ParamKnob id="glide" ctx={ctx} label="Glide" size="sm" format={fmt.glide} />
        </div>
        <ParamKnob id="volume" ctx={ctx} label="Volume" size="lg" defaultValue={0.8} format={fmt.volume} />
      </div>
    </Panel>
  );
}
