import { useCallback, useState } from "react";
import { Keybed, Wheel, type KeybedNoteMask } from "@xerum/ui";
import { noteMaskOf } from "../../juce/backend";
import { useBackend } from "../../juce/provider";
import { PerformanceBar } from "./PerformanceBar";

const OCTAVES = 4;
const LOWEST = 0;
const HIGHEST = 127 - 12 * OCTAVES;

/**
 * La striscia bassa: rotelle a sinistra, barra di esecuzione e tasti a destra.
 *
 * Le rotelle stanno in una colonna a tutta altezza e non dentro la barra perche' hanno bisogno di
 * corsa verticale: in 28 px non si suona niente.
 */
export function BottomStrip() {
  const backend = useBackend();
  const [firstNote, setFirstNote] = useState(36); // C2, la stessa nota da cui partiva la striscia nativa
  const [velocity, setVelocity] = useState(80);
  const [pitch, setPitch] = useState(0.5);
  const [mod, setMod] = useState(0);

  // Non un clamp: Math.min/Math.max fermerebbe firstNote a un valore qualunque dentro il range
  // (es. 79, un Sol), spostando i tasti neri fuori posto rispetto al readout. Se il salto
  // uscirebbe dal range MIDI valido lo shift non fa nulla, cosi' firstNote resta sempre un
  // multiplo di 12 di distanza da 36.
  const shift = (by: number) =>
    setFirstNote((n) => {
      const next = n + by;
      return next >= LOWEST && next <= HIGHEST ? next : n;
    });

  const onPitch = useCallback((v: number) => { setPitch(v); void backend.setWheel("pitch", v); }, [backend]);
  const onMod = useCallback((v: number) => { setMod(v); void backend.setWheel("mod", v); }, [backend]);

  const onNoteOn = useCallback((note: number, vel: number) => { void backend.noteOn(note, vel); }, [backend]);
  const onNoteOff = useCallback((note: number) => { void backend.noteOff(note); }, [backend]);
  const onAllNotesOff = useCallback(() => { void backend.allNotesOff(); }, [backend]);

  // Non passa da useMeterFrame: leggere quel contesto qui rirenderizzerebbe la striscia trenta
  // volte al secondo, che e' esattamente cio' che Keybed evita mutando data-active via ref.
  const subscribeNotes = useCallback(
    (cb: (mask: KeybedNoteMask) => void) => backend.onMeters((m) => cb(noteMaskOf(m))),
    [backend],
  );

  return (
    <div className="flex h-[108px] shrink-0 gap-2">
      <div className="flex w-16 shrink-0 gap-2 py-1">
        <Wheel value={pitch} onChange={onPitch} label="PB" defaultValue={0.5} bipolar springBack tone="osc" />
        <Wheel value={mod} onChange={onMod} label="MW" tone="lfo" />
      </div>

      <div className="flex min-w-0 flex-1 flex-col">
        <PerformanceBar
          firstNote={firstNote}
          onOctaveDown={() => shift(-12)}
          onOctaveUp={() => shift(12)}
          velocity={velocity}
          onVelocityChange={setVelocity}
        />
        <Keybed
          firstNote={firstNote}
          octaves={OCTAVES}
          velocity={velocity / 127}
          onNoteOn={onNoteOn}
          onNoteOff={onNoteOff}
          onAllNotesOff={onAllNotesOff}
          subscribeNotes={subscribeNotes}
          className="flex-1"
        />
      </div>
    </div>
  );
}
