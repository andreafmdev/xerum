import { useMemo } from "react";
import { Panel, Segmented, Stepper } from "@xerum/ui";
import { ChevronLeft, ChevronRight } from "lucide-react";
import { useBoolParam, useChoiceParam, useFloatParam, useIntParam } from "../../juce/hooks";
import { filterPath } from "../curves";
import { signedInt } from "../mapping";
import { liveValue, modsFor, type FilterType } from "../mod";
import { step } from "../presets";
import { useSourceLevels } from "./MetersContext";
import { ParamKnob } from "./ParamKnob";
import { useDirty, useSynthCtx } from "./SynthContext";

const plate = "sx-plate min-w-0 gap-1.5 pt-1.5";
const body = "flex flex-1 items-end justify-between gap-1 px-2.5 pb-2.5";

export function OscPanel() {
  const oscOn = useBoolParam("oscOn");
  const unison = useChoiceParam("unison");
  const wt = useChoiceParam("wtIndex");
  const oct = useIntParam("oct");
  const semi = useIntParam("semi");
  const dirty = useDirty();
  const wtIndex = wt.options.findIndex((o) => o.value === wt.value);
  const pickWt = dirty((delta: number) => wt.set(wt.options[step(wtIndex, delta, wt.options.length)]!.value));
  return (
    <Panel
      title="Oscillator"
      tone="osc"
      className={`${plate} flex-[0_0_430px]`}
      on={oscOn.checked}
      onToggle={dirty(oscOn.set)}
      actions={
        <>
          <Segmented label="Unison voices" value={unison.value} onChange={dirty(unison.set)} options={unison.options} />
          <span className="text-2xs tracking-wider text-text-dim">UNISON</span>
        </>
      }
    >
      <div className={body}>
        <div className="flex h-full w-32 shrink-0 flex-col justify-between gap-1.5 self-stretch">
          <div className="sx-well flex h-6.5 items-center gap-1 rounded-control bg-well px-1.5 shadow-well">
            <button type="button" aria-label="Previous wavetable" onClick={() => pickWt(-1)} className="text-text-dim hover:text-foreground">
              <ChevronLeft className="size-3.5" />
            </button>
            <span className="flex-1 truncate text-center font-mono text-2xs">{wt.options[wtIndex]?.label}</span>
            <button type="button" aria-label="Next wavetable" onClick={() => pickWt(1)} className="text-text-dim hover:text-foreground">
              <ChevronRight className="size-3.5" />
            </button>
          </div>
          <div className="flex flex-col gap-1.5">
            <Stepper value={oct.value} onChange={dirty(oct.set)} min={oct.min} max={oct.max} unit="OCT" label="Octave" format={signedInt} />
            <Stepper value={semi.value} onChange={dirty(semi.set)} min={semi.min} max={semi.max} unit="SEMI" label="Semitones" format={signedInt} />
          </div>
        </div>
        <ParamKnob id="wtpos" size="lg" />
        <ParamKnob id="warp" />
        <ParamKnob id="detune" />
        <ParamKnob id="fine" />
        <ParamKnob id="level" />
      </div>
    </Panel>
  );
}

const CURVE_W = 236;
const CURVE_H = 58;

/**
 * Risposta del filtro, in un componente a parte: è l'unica cosa del pannello che
 * segue la cutoff modulata, quindi solo lei si abbona ai meter.
 */
function FilterCurve({ cutoff, res, type }: { cutoff: number; res: number; type: FilterType }) {
  const { mods } = useSynthCtx();
  const sources = useSourceLevels();
  const cutLive = liveValue(cutoff, modsFor(mods, "cutoff"), sources);
  const d = useMemo(() => filterPath(cutLive, res, type, CURVE_W, CURVE_H), [cutLive, res, type]);
  return (
    <div className="sx-well mx-2.5 overflow-hidden rounded-control bg-well shadow-well" style={{ height: CURVE_H }}>
      <svg width="100%" height={CURVE_H} viewBox={`0 0 ${CURVE_W} ${CURVE_H}`} preserveAspectRatio="none" className="block">
        {[0.25, 0.5, 0.75].map((x) => (
          <line key={x} x1={x * CURVE_W} x2={x * CURVE_W} y1="0" y2={CURVE_H} className="stroke-line-strong" opacity={0.3} />
        ))}
        <path d={`${d} L${CURVE_W} ${CURVE_H} L0 ${CURVE_H} Z`} className="fill-(--tone)" opacity={0.12} />
        <path d={d} className="fill-none stroke-(--tone) [filter:var(--glow)]" strokeWidth={1.8} />
      </svg>
    </div>
  );
}

export function FilterPanel() {
  const filtOn = useBoolParam("filtOn");
  const ftype = useChoiceParam("ftype");
  const slope = useChoiceParam("slope");
  const cutoff = useFloatParam("cutoff");
  const res = useFloatParam("res");
  const dirty = useDirty();
  return (
    <Panel
      title="Filter"
      tone="filter"
      className={`${plate} flex-[1_1_0]`}
      on={filtOn.checked}
      onToggle={dirty(filtOn.set)}
      actions={
        <>
          <Segmented label="Filter type" value={ftype.value} onChange={dirty(ftype.set)} options={ftype.options} />
          <Segmented label="Slope" value={slope.value} onChange={dirty(slope.set)} options={slope.options} />
        </>
      }
    >
      <FilterCurve cutoff={cutoff.value} res={res.value} type={ftype.value as FilterType} />
      <div className={`${body} justify-around`}>
        <ParamKnob id="cutoff" size="lg" disabled={!filtOn.checked} />
        <ParamKnob id="res" disabled={!filtOn.checked} />
        <ParamKnob id="drive" disabled={!filtOn.checked} />
        <ParamKnob id="keytrk" disabled={!filtOn.checked} />
      </div>
    </Panel>
  );
}

export function MasterPanel() {
  const voiceMode = useChoiceParam("voiceMode");
  const dirty = useDirty();
  return (
    <Panel title="Master" tone="master" className={`${plate} flex-[0_0_176px]`}>
      <div className="px-2.5">
        <Segmented label="Voice mode" value={voiceMode.value} onChange={dirty(voiceMode.set)} options={voiceMode.options} />
      </div>
      <div className={`${body} justify-around`}>
        <div className="flex gap-2">
          <ParamKnob id="pan" size="sm" />
          <ParamKnob id="glide" size="sm" />
        </div>
        <ParamKnob id="volume" size="lg" />
      </div>
    </Panel>
  );
}
