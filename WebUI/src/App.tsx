import { useState } from "react";
import {
  Button,
  Fader,
  Knob,
  Panel,
  Select,
  Tabs,
  Toggle,
  ValueReadout,
  WavetableDisplay,
} from "@xerum/ui";

const N = 128;
const frames = Array.from({ length: 16 }, (_, f) => {
  const mix = f / 15;
  return Float32Array.from({ length: N }, (_, i) => {
    const t = i / N;
    return Math.sin(t * Math.PI * 2) * (1 - mix) + (2 * t - 1) * mix;
  });
});

const hz = (v: number) => `${Math.round(20 * Math.pow(1000, v))} Hz`;
const pct = (v: number) => `${Math.round(v * 100)}%`;

export default function App() {
  const [osc, setOsc] = useState("a");
  const [wtPos, setWtPos] = useState(0.3);
  const [oscLevel, setOscLevel] = useState(0.8);
  const [table, setTable] = useState<string | null>("basic");
  const [cutoff, setCutoff] = useState(0.6);
  const [res, setRes] = useState(0.2);
  const [filterOn, setFilterOn] = useState(true);
  const [attack, setAttack] = useState(0.05);
  const [decay, setDecay] = useState(0.3);
  const [sustain, setSustain] = useState(0.7);
  const [release, setRelease] = useState(0.4);
  const [master, setMaster] = useState(0.75);

  return (
    <main className="flex min-h-screen flex-col gap-4 p-6">
      <header className="flex items-center justify-between">
        <div className="flex flex-col">
          <span className="text-2xs tracking-widest text-primary uppercase">SerumStyleSynth · Phase 1</span>
          <h1 className="text-xl font-semibold">Wavetable synth scaffold</h1>
        </div>
        <div className="flex items-center gap-3">
          <ValueReadout label="bridge" value="stub" />
          <Button variant="outline" size="sm">Init</Button>
        </div>
      </header>

      <div className="grid grid-cols-1 gap-4 md:grid-cols-3">
        <Panel title="Oscillator" tone="osc" actions={<Tabs value={osc} onChange={setOsc} items={[{ value: "a", label: "A" }, { value: "b", label: "B" }]} />}>
          <Select value={table} onChange={setTable} label="Wavetable" options={[{ value: "basic", label: "Basic Shapes" }, { value: "analog", label: "Analog Classics" }]} />
          <WavetableDisplay frames={frames} position={wtPos} />
          <div className="flex gap-4">
            <Knob value={wtPos} onChange={setWtPos} label="WT pos" />
            <Knob value={oscLevel} onChange={setOscLevel} label="Level" format={pct} />
          </div>
        </Panel>

        <Panel title="Filter" tone="filter" actions={<Toggle checked={filterOn} onChange={setFilterOn} label="on" />}>
          <div className="flex gap-4">
            <Knob value={cutoff} onChange={setCutoff} label="Cutoff" format={hz} size="lg" disabled={!filterOn} />
            <Knob value={res} onChange={setRes} label="Res" disabled={!filterOn} />
          </div>
        </Panel>

        <Panel title="Amp Env" tone="env">
          <div className="flex gap-4">
            <Fader value={attack} onChange={setAttack} label="A" />
            <Fader value={decay} onChange={setDecay} label="D" />
            <Fader value={sustain} onChange={setSustain} label="S" />
            <Fader value={release} onChange={setRelease} label="R" />
          </div>
        </Panel>
      </div>

      <Panel title="Master" tone="master" className="md:w-64">
        <Fader value={master} onChange={setMaster} label="Out" orientation="horizontal" format={(v) => `${(v * 12 - 6).toFixed(1)} dB`} defaultValue={0.5} />
      </Panel>
    </main>
  );
}
