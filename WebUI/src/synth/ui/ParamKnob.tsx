import { useCallback, useEffect, useMemo, useRef } from "react";
import { Knob, type KnobProps } from "@xerum/ui";
import { defaultNormalised, type ModAssignment } from "../../juce/backend";
import { useFloatParam } from "../../juce/hooks";
import { formatValue } from "../mapping";
import { modsFor, SOURCE_TONE, type ModSource } from "../mod";
import type { ParamId } from "../params.generated";
import { useLiveValue } from "./meters";
import { useSynthCtx } from "./SynthContext";

type Props = Omit<KnobProps, "value" | "onChange" | "onChangeEnd" | "mods" | "liveValue" | "onDropMod" | "label"> & {
  id: ParamId;
  label?: string;
};

/** Knob legato a un parametro del bridge: valore, gesture, anelli e puntino live dalla mod matrix. */
export function ParamKnob({ id, label, format, bipolar, ...rest }: Props) {
  const p = useFloatParam(id);
  const { mods, addMod, markDirty } = useSynthCtx();
  const mine = useMemo(() => modsFor(mods, id), [mods, id]);
  const spec = p.spec;
  const defaultFormat = useCallback((v: number) => formatValue(spec, v), [spec]);

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

  const knob: KnobProps = {
    value: p.value,
    onChange,
    onChangeEnd,
    label: label ?? p.spec.name,
    format: format ?? defaultFormat,
    bipolar: bipolar ?? p.spec.bipolar,
    onDropMod: (src) => addMod(src as ModSource, id),
    ...rest,
    // Il doppio clic rimanda al default: senza questo useDragValue userebbe 0 e
    // scriverebbe una gesture reale a zero verso l'host.
    defaultValue: rest.defaultValue ?? defaultNormalised(p.spec),
  };

  // Solo i knob modulati si abbonano ai meter: gli altri non si ridisegnano a 30 Hz.
  return mine.length ? <ModulatedKnob knob={knob} mods={mine} /> : <Knob {...knob} />;
}

/** Knob con anelli e puntino live: l'unico che segue il livello istantaneo delle sorgenti. */
function ModulatedKnob({ knob, mods }: { knob: KnobProps; mods: ModAssignment[] }) {
  // Gli anelli cambiano con la mod matrix, non con i meter; il puntino live e' l'unico valore
  // selezionato dal frame, quindi il knob ri-renderizza solo quando quel numero cambia.
  const rings = useMemo(() => mods.map((m) => ({ tone: SOURCE_TONE[m.src], depth: m.depth, bipolar: m.src === "lfo" })), [mods]);
  const live = useLiveValue(knob.value, mods);
  return <Knob {...knob} mods={rings} liveValue={live} />;
}
