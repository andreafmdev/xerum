import { useCallback, useEffect, useRef, useState } from "react";
import { Keybed, Wheel, type KeybedNoteMask } from "@xerum/ui";
import { noteMaskOf } from "../../juce/backend";
import { useBackend } from "../../juce/provider";
import { PerformanceBar } from "./PerformanceBar";

const OCTAVES = 4;
const LOWEST = 0;
const HIGHEST = 127 - 12 * OCTAVES;

// Suonare dalla tastiera del computer. Prima del ramo lo faceva la striscia nativa
// (`setKeyPressBaseOctave (5)` in PluginEditor), e la spec la mette fra i rischi accettati come
// una capacita' che *diventa* un keydown JS, non che sparisce: eccolo.
//
// Le due file sono quelle standard di tracker e plugin: `A W S E D F T G Y H U J` copre
// un'ottava dal primo tasto visibile, `K O L P ;` continua sopra. Si guarda `event.code` e non
// `event.key` perche' `code` e' la *posizione fisica* del tasto, ed e' la posizione a contare
// quando la tastiera del computer fa da tastiera musicale: le due file restano sotto le stesse
// dita anche su un layout non QWERTY.
const KEY_SEMITONES: Record<string, number> = {
  KeyA: 0, KeyW: 1, KeyS: 2, KeyE: 3, KeyD: 4, KeyF: 5, KeyT: 6,
  KeyG: 7, KeyY: 8, KeyH: 9, KeyU: 10, KeyJ: 11,
  KeyK: 12, KeyO: 13, KeyL: 14, KeyP: 15, Semicolon: 16,
};

/** Chi sta scrivendo si tiene i suoi tasti: la "a" nella ricerca preset non deve suonare un DO. */
const typingInto = (t: EventTarget | null) =>
  t instanceof HTMLElement
  && (t.isContentEditable || t.tagName === "INPUT" || t.tagName === "TEXTAREA" || t.tagName === "SELECT");

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

  // Le note tenute dalla tastiera del computer, per `code` premuto. Un ref e non uno stato: non
  // c'e' niente da ridisegnare — i tasti si accendono dal mask che torna dal motore, come per il
  // puntatore — e il gestore di keyup deve leggere sempre l'ultima versione.
  const typed = useRef(new Map<string, number>());
  useEffect(() => {
    const down = (e: KeyboardEvent) => {
      // `repeat` e' l'auto-ripetizione del sistema: non e' una nota nuova. E' un'uscita anticipata
      // esplicita, non l'unica difesa — il `typed` qui sotto blocca comunque un `code` gia' in
      // mano, ripetizione o no. I modificatori sono scorciatoie dell'host o del browser, non note.
      if (e.repeat || e.ctrlKey || e.metaKey || e.altKey) return;
      if (typingInto(e.target)) return;
      const semi = KEY_SEMITONES[e.code];
      if (semi === undefined || typed.current.has(e.code)) return;
      const note = firstNote + semi;
      if (note > 127) return;
      typed.current.set(e.code, note);
      // La stessa strada del puntatore, velocity compresa: Keybed passa `velocity / 127` perche'
      // il bridge vuole 0..1.
      onNoteOn(note, velocity / 127);
    };
    const up = (e: KeyboardEvent) => {
      // Si spegne la nota registrata al keydown, non quella che `firstNote` darebbe adesso:
      // cambiare ottava con un tasto premuto non deve lasciare la nota appesa.
      const note = typed.current.get(e.code);
      if (note === undefined) return;
      typed.current.delete(e.code);
      onNoteOff(note);
    };
    // Perdendo il fuoco la finestra non manda nessun keyup, e le note resterebbero appese per
    // sempre: stesso panic che Keybed fa per il puntatore.
    const panic = () => { typed.current.clear(); onAllNotesOff(); };

    window.addEventListener("keydown", down);
    window.addEventListener("keyup", up);
    window.addEventListener("blur", panic);
    return () => {
      window.removeEventListener("keydown", down);
      window.removeEventListener("keyup", up);
      window.removeEventListener("blur", panic);
    };
  }, [firstNote, velocity, onNoteOn, onNoteOff, onAllNotesOff]);

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
