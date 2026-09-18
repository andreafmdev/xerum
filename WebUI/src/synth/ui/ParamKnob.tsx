import { Knob, type KnobProps } from "@xerum/ui";
import { liveValue, modsFor, SOURCE_TONE, type ModAssignment, type ModSource, type SourceLevels } from "../mod";
import type { KnobId, SynthParams } from "../params";

export type SynthKnobCtx = {
  p: SynthParams;
  set: <K extends keyof SynthParams>(id: K, v: SynthParams[K]) => void;
  mods: ModAssignment[];
  sources: SourceLevels;
  addMod: (src: ModSource, target: KnobId) => void;
};

type Props = Omit<KnobProps, "value" | "onChange" | "mods" | "liveValue" | "onDropMod"> & {
  id: KnobId;
  ctx: SynthKnobCtx;
};

/** Knob legato a un parametro: valore, anelli e puntino live dalla mod matrix, bersaglio di drop. */
export function ParamKnob({ id, ctx, ...rest }: Props) {
  const mods = modsFor(ctx.mods, id);
  const value = ctx.p[id];
  return (
    <Knob
      value={value}
      onChange={(v) => ctx.set(id, v)}
      mods={mods.map((m) => ({ tone: SOURCE_TONE[m.src], depth: m.depth, bipolar: m.src === "lfo" }))}
      liveValue={mods.length ? liveValue(value, mods, ctx.sources) : undefined}
      onDropMod={(src) => ctx.addMod(src as ModSource, id)}
      {...rest}
    />
  );
}
