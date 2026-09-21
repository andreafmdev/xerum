import { useMemo } from "react";
import { Panel, Segmented, Stepper } from "@xerum/ui";
import { ChevronLeft, ChevronRight } from "lucide-react";
import { useBoolParam, useChoiceParam, useFloatParam, useIntParam } from "../../juce/hooks";
import { filterPath } from "../curves";
import { signedInt } from "../mapping";
import { modsFor, type FilterType } from "../mod";
import { step } from "../presets";
import { useLiveValue } from "./meters";
import { GlowCurve } from "./GlowCurve";
import { ParamKnob } from "./ParamKnob";
import { useDirty, useSynthCtx } from "./SynthContext";

const plate = "sx-plate min-w-0 gap-1.5 pt-1.5";
// Niente padding qui: CardContent di Panel ne mette gia' 12 px per lato e 12 in fondo, e il
// px-2.5/pb-2.5 che c'era li raddoppiava a 22 — venti px di pannello buttati per lato, che e'
// buona parte del motivo per cui i knob del design non entravano. Il design dà al pannello
// `padding: 8px 10px 10px`: i 12 della libreria sono quelli, non ventidue.
const body = "flex flex-1 items-end justify-between gap-1";

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
// 56, non 58: il pannello da 228 si chiude cosi' al pixel — 12 di padding alto, 24 di
// intestazione, 6 di gap, poi 56 di curva, 12 di gap, 106 di knob e 12 di padding basso.
// Misurato in Chromium; a 58 il pannello traboccava di 2 px e la riga bassa dei readout
// finiva sotto l'overflow-hidden della piastra.
const CURVE_H = 56;

/**
 * Risposta del filtro, in un componente a parte: è l'unica cosa del pannello che
 * segue la cutoff modulata, quindi solo lei si abbona ai meter.
 */
function FilterCurve({ cutoff, res, type }: { cutoff: number; res: number; type: FilterType }) {
  const { mods } = useSynthCtx();
  const cutLive = useLiveValue(cutoff, modsFor(mods, "cutoff"));
  const d = useMemo(() => filterPath(cutLive, res, type, CURVE_W, CURVE_H), [cutLive, res, type]);
  return (
    <div className="sx-well overflow-hidden rounded-control bg-well shadow-well" style={{ height: CURVE_H }}>
      <svg width="100%" height={CURVE_H} viewBox={`0 0 ${CURVE_W} ${CURVE_H}`} preserveAspectRatio="none" className="block">
        {[0.25, 0.5, 0.75].map((x) => (
          <line key={x} x1={x * CURVE_W} x2={x * CURVE_W} y1="0" y2={CURVE_H} className="stroke-line-strong" opacity={0.3} />
        ))}
        <GlowCurve d={d} close={`L${CURVE_W} ${CURVE_H} L0 ${CURVE_H} Z`} />
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
  // 176 px, non i 200 del design: sono esattamente quelli che il contenuto occupa (due knob `sm`
  // affiancati, il knob `lg` del volume, gap e padding), e i 24 di differenza servono al Filter,
  // che con le etichette lunghe ("Resonance") ne chiede 252 dei 254 che restano. Misurato in
  // Chromium: a 200 il Filter trabocca di 22 px.
  return (
    <Panel title="Master" tone="master" className={`${plate} flex-[0_0_176px]`}>
      <div>
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
