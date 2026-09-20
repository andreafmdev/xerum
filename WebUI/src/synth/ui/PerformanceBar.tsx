import { Button, Meter, Select, Stepper } from "@xerum/ui";
import { noteMaskOf } from "../../juce/backend";
import { useMeterValue, useMidiInputs } from "../../juce/hooks";

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
 * La sorgente MIDI: nel plugin la scritta "HOST" (il MIDI arriva dall'host), nello Standalone un
 * selettore degli ingressi del sistema (MidiSource); accanto, una spia di attivita' presa dal mask
 * delle note.
 *
 * I meter arrivano a 30 Hz: li leggono solo i due figli qui sotto, con un selettore, cosi'
 * bottoni e stepper non ri-renderizzano mai per un frame e la spia solo quando cambia stato.
 */
export function PerformanceBar({
  firstNote,
  onOctaveDown,
  onOctaveUp,
  velocity,
  onVelocityChange,
}: PerformanceBarProps) {
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

      <MidiSource />
      <MidiActivity />

      <span className="flex-1" />

      <span>IN</span>
      <IoMeters />
      <span>OUT</span>
    </div>
  );
}

/**
 * "MIDI HOST" nel plugin; nello Standalone il selettore degli ingressi: "Tutti gli ingressi" li
 * abilita tutti, un device abilita solo lui. Mostra il device quando e' l'unico acceso, altrimenti
 * "Tutti gli ingressi": e' la lettura piu' onesta di un insieme che l'AudioDeviceManager tiene
 * per device.
 */
function MidiSource() {
  const { inputs, select } = useMidiInputs();
  if (inputs.host) return <span data-testid="midi-source">MIDI HOST</span>;
  const enabled = inputs.devices.filter((d) => d.enabled);
  const value = enabled.length === 1 ? enabled[0]!.id : "all";
  const options = [{ value: "all", label: "Tutti gli ingressi" }, ...inputs.devices.map((d) => ({ value: d.id, label: d.name }))];
  return (
    <span className="flex items-center gap-1.5">
      <span>MIDI</span>
      <Select label="MIDI input" value={value} onChange={(v) => void select(v)} options={options} className="h-5 min-w-28 text-2xs" />
    </span>
  );
}

/** La spia MIDI: accesa se una qualunque delle quattro parole del mask ha almeno un bit alto. */
function MidiActivity() {
  const anyNote = useMeterValue((f) => noteMaskOf(f).some((word) => word !== 0));
  return (
    <span
      data-testid="midi-activity"
      data-on={anyNote}
      aria-hidden
      className="size-1.5 rounded-full bg-tick data-[on=true]:bg-(--tone)"
    />
  );
}

function IoMeters() {
  const input = useMeterValue((f) => f.in);
  const output = useMeterValue((f) => f.out);
  return (
    <>
      <Meter level={input} label="Input" tone="osc" />
      <Meter level={output} label="Output" tone="master" />
    </>
  );
}
