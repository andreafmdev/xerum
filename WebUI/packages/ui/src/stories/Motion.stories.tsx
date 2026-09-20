import type { Meta, StoryObj } from "@storybook/react-vite";
import { useState } from "react";
import { Button } from "@/components/Button/Button";
import { Knob } from "@/components/Knob/Knob";
import { Segmented } from "@/components/Segmented/Segmented";
import { Tabs } from "@/components/Tabs/Tabs";
import { Toggle } from "@/components/Toggle/Toggle";
// synth.css vive nell'app (WebUI/src), non nel package @xerum/ui: qui è raggiungibile solo
// con un percorso relativo che esce dal package. L'import risolve (Storybook di questo
// package gira sotto lo stesso Vite, senza sandboxing sui path fuori radice), quindi si
// prende questa strada invece di ridurre la story alla sola variante di default.
import "../../../../src/synth/ui/synth.css";

const VARIANTS = ["soft", "deep", "glow", "glass", "metal"] as const;

/** Tutti gli stati animati in una schermata: è qui che il motion si giudica a occhio,
    perché jsdom non anima e un test può solo verificare i contratti. */
function Bench() {
  const [seg, setSeg] = useState("a");
  const [tab, setTab] = useState("env");
  const [on, setOn] = useState(false);
  const [v, setV] = useState(0.3);
  return (
    <div className="flex flex-col gap-4 p-4">
      <div className="flex items-center gap-3">
        <Button tone="osc">Load</Button>
        <Button variant="secondary">Save</Button>
        <Button variant="outline">Init</Button>
        <Toggle checked={on} onChange={setOn} label="Sync" tone="lfo" />
      </div>
      <Segmented
        label="Mode"
        value={seg}
        onChange={setSeg}
        options={[
          { value: "a", label: "TRI" },
          { value: "b", label: "SAW" },
          { value: "c", label: "SQR" },
        ]}
        tone="osc"
      />
      <Tabs
        variant="bar"
        value={tab}
        onChange={setTab}
        items={[
          { value: "env", label: "ENV", tone: "env" },
          { value: "lfo", label: "LFO", tone: "lfo" },
          { value: "fx", label: "FX", tone: "fx" },
        ]}
      />
      <div className="flex gap-4">
        <Knob value={v} onChange={setV} label="Cutoff" tone="filter" />
        <Knob value={v} onChange={setV} label="Depth" tone="lfo" mods={[{ tone: "lfo", depth: 0.25 }]} liveValue={v} />
      </div>
    </div>
  );
}

const meta = {
  title: "Motion/Bench",
  component: Bench,
} satisfies Meta<typeof Bench>;

export default meta;
type Story = StoryObj<typeof meta>;

/** Una story per variante: le curve cambiano con lo chassis, quindi vanno viste tutte. */
export const AllVariants: Story = {
  render: () => (
    <div className="grid grid-cols-2 gap-4">
      {VARIANTS.map((variant) => (
        <div key={variant} className="sx-chassis" data-variant={variant} style={{ width: 420, height: "auto" }}>
          <p className="px-4 pt-3 font-mono text-2xs tracking-widest text-text-dim uppercase">{variant}</p>
          <Bench />
        </div>
      ))}
    </div>
  ),
};
