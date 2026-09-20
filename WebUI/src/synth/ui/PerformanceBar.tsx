import { Button, Meter, Stepper } from "@xerum/ui";
import { noteMaskOf } from "../../juce/backend";
import { useMeterFrame } from "./MetersContext";

export type PerformanceBarProps = {
  /** Nota MIDI del primo tasto visibile. */
  firstNote: number;
  onOctaveDown: () => void;
  onOctaveUp: () => void;
  /** Velocity 1..127 mostrata e regolata dalla barra. */
  velocity: number;
  onVelocityChange: (v: number) => void;
};

const NAMES = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"];

/** Nome di una nota MIDI con la convenzione in cui il DO centrale (60) e' C4. */
const noteName = (n: number) => `${NAMES[n % 12]}${Math.floor(n / 12) - 1}`;

/**
 * Barra di esecuzione sopra i tasti: ottava, velocity, stato MIDI e i due meter.
 *
 * Ha preso il posto del Footer, che portava anche VOICES e CPU: erano numeri inventati — lo
 * diceva il commento del file stesso — e sono spariti invece di essere trasportati.
 *
 * Lo spazio della sorgente MIDI mostra oggi solo "HOST" e una spia di attivita' presa dal mask
 * delle note. E' il punto in cui atterrera' la scelta del device con il sottoprogetto B.
 */
export function PerformanceBar({
  firstNote,
  onOctaveDown,
  onOctaveUp,
  velocity,
  onVelocityChange,
}: PerformanceBarProps) {
  const m = useMeterFrame();
  // Nessuna nota singola da controllare qui: basta sapere se una qualunque delle quattro parole
  // del mask ha almeno un bit acceso.
  const anyNote = noteMaskOf(m).some((word) => word !== 0);

  return (
    <div className="flex h-7 shrink-0 items-center gap-3 px-1.5 font-mono text-2xs tracking-wider text-text-dim">
      <span>OCT</span>
      <Button size="sm" aria-label="Octave down" onClick={onOctaveDown}>
        −
      </Button>
      <span data-testid="octave-readout" className="w-8 text-center font-medium text-foreground">
        {noteName(firstNote)}
      </span>
      <Button size="sm" aria-label="Octave up" onClick={onOctaveUp}>
        +
      </Button>

      <Stepper value={velocity} onChange={onVelocityChange} min={1} max={127} label="Velocity" unit="VEL" />

      <span className="flex-1" />

      <span data-testid="midi-source">MIDI HOST</span>
      <span
        data-testid="midi-activity"
        data-on={anyNote}
        aria-hidden
        className="size-1.5 rounded-full bg-tick data-[on=true]:bg-(--tone)"
      />

      <span className="flex-1" />

      <span>IN</span>
      <Meter level={m.in} label="Input" tone="osc" />
      <Meter level={m.out} label="Output" tone="master" />
      <span>OUT</span>
    </div>
  );
}
