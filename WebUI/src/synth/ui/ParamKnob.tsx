import { useEffect, useRef } from "react";
import { Knob, type KnobProps } from "@xerum/ui";
import { useFloatParam } from "../../juce/hooks";
import { formatValue } from "../mapping";
import { liveValue, modsFor, SOURCE_TONE, type ModSource } from "../mod";
import type { ParamId } from "../params.generated";
import { useSynthCtx } from "./SynthContext";

type Props = Omit<KnobProps, "value" | "onChange" | "onChangeEnd" | "mods" | "liveValue" | "onDropMod" | "label"> & {
  id: ParamId;
  label?: string;
};

/** Knob legato a un parametro del bridge: valore, gesture, anelli e puntino live dalla mod matrix. */
export function ParamKnob({ id, label, format, bipolar, ...rest }: Props) {
  const p = useFloatParam(id);
  const { mods, addMod, sources, markDirty } = useSynthCtx();
  const mine = modsFor(mods, id);

  // Gesture: begin al primo onChange di un drag, end quando il Knob smette di trascinare.
  const inGesture = useRef(false);
  const onChange = (v: number) => {
    if (!inGesture.current) {
      inGesture.current = true;
      p.begin();
    }
    p.set(v);
    markDirty();
  };
  const onChangeEnd = () => {
    if (inGesture.current) {
      inGesture.current = false;
      p.end();
    }
  };
  // Smontaggio a gesture aperta (cambio tab mentre si trascina): chiudila lo stesso,
  // altrimenti l'host resterebbe in automazione.
  const end = useRef(p.end);
  end.current = p.end;
  useEffect(() => () => { if (inGesture.current) end.current(); }, []);

  return (
    <Knob
      value={p.value}
      onChange={onChange}
      onChangeEnd={onChangeEnd}
      label={label ?? p.spec.name}
      format={format ?? ((v) => formatValue(p.spec, v))}
      bipolar={bipolar ?? p.spec.bipolar}
      mods={mine.map((m) => ({ tone: SOURCE_TONE[m.src], depth: m.depth, bipolar: m.src === "lfo" }))}
      liveValue={mine.length ? liveValue(p.value, mine, sources) : undefined}
      onDropMod={(src) => addMod(src as ModSource, id)}
      {...rest}
    />
  );
}
