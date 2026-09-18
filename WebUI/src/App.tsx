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
const signed = (v: number) => {
  const amount = Math.round(v * 200 - 100);
  return `${amount > 0 ? "+" : ""}${amount}%`;
};
const db = (v: number) => `${(v * 12 - 6).toFixed(1)} dB`;

export default function App() {
  const [osc, setOsc] = useState("a");
  const [wtPos, setWtPos] = useState(0.3);
  const [oscLevel, setOscLevel] = useState(0.8);
  const [table, setTable] = useState<string | null>("basic");
  const [cutoff, setCutoff] = useState(0.6);
  const [res, setRes] = useState(0.2);
  const [filterType, setFilterType] = useState<string | null>("lp24");
  const [envAmt, setEnvAmt] = useState(0.65);
  const [filterOn, setFilterOn] = useState(true);
  const [attack, setAttack] = useState(0.05);
  const [decay, setDecay] = useState(0.3);
  const [sustain, setSustain] = useState(0.7);
  const [release, setRelease] = useState(0.4);
  const [master, setMaster] = useState(0.75);

  return (
    <main className="flex min-h-screen flex-col gap-3 p-4">
      <header className="flex items-center justify-between gap-4">
        <h1 className="text-xs font-semibold tracking-widest text-muted-foreground uppercase [text-shadow:0_1px_0_var(--color-edge-dark)]">
          Xerum
        </h1>
        <div className="flex items-center gap-3">
          <ValueReadout label="Bridge" value="stub" />
          <Button variant="outline" size="sm">Init</Button>
        </div>
      </header>

      <div className="grid grid-cols-1 gap-3 md:grid-cols-2 lg:grid-cols-[1.4fr_1fr_1fr_0.8fr]">
        <Panel
          title="Oscillator"
          tone="osc"
          actions={<Tabs value={osc} onChange={setOsc} items={[{ value: "a", label: "A" }, { value: "b", label: "B" }]} />}
        >
          <Select
            value={table}
            onChange={setTable}
            label="Wavetable"
            options={[{ value: "basic", label: "Basic Shapes" }, { value: "analog", label: "Analog Classics" }]}
          />
          <WavetableDisplay frames={frames} position={wtPos} />
          <div className="flex items-center justify-center gap-6">
            <Knob value={wtPos} onChange={setWtPos} label="WT pos" />
            <Knob value={oscLevel} onChange={setOscLevel} label="Level" format={pct} />
          </div>
        </Panel>

        <Panel title="Filter" tone="filter" actions={<Toggle checked={filterOn} onChange={setFilterOn} label="On" />}>
          <Select
            value={filterType}
            onChange={setFilterType}
            label="Filter type"
            disabled={!filterOn}
            options={[
              { value: "lp24", label: "Low pass 24" },
              { value: "lp12", label: "Low pass 12" },
              { value: "hp12", label: "High pass 12" },
              { value: "bp12", label: "Band pass 12" },
            ]}
          />
          <div className="flex flex-1 flex-wrap items-center justify-center gap-5">
            <Knob value={cutoff} onChange={setCutoff} label="Cutoff" format={hz} size="lg" disabled={!filterOn} />
            <Knob value={res} onChange={setRes} label="Res" disabled={!filterOn} />
            <Knob
              value={envAmt}
              onChange={setEnvAmt}
              label="Env amt"
              format={signed}
              bipolar
              disabled={!filterOn}
            />
          </div>
        </Panel>

        <Panel title="Amp Env" tone="env">
          <div className="flex flex-1 items-center justify-between gap-2">
            <Fader value={attack} onChange={setAttack} label="Attack" />
            <Fader value={decay} onChange={setDecay} label="Decay" />
            <Fader value={sustain} onChange={setSustain} label="Sustain" />
            <Fader value={release} onChange={setRelease} label="Release" />
          </div>
        </Panel>

        <Panel title="Master" tone="master">
          <div className="flex flex-1 items-center justify-center">
            <Fader value={master} onChange={setMaster} label="Out" format={db} defaultValue={0.5} />
          </div>
        </Panel>
      </div>
    </main>
  );
}
