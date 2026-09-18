import { useState } from "react";
import type { Meta, StoryObj } from "@storybook/react-vite";
import { Panel } from "./Panel";
import { Knob } from "@/components/Knob/Knob";
import { Toggle } from "@/components/Toggle/Toggle";
import { Select } from "@/components/Select/Select";

const meta = {
  title: "Layout/Panel",
  component: Panel,
  args: { title: "Filter", tone: "filter", children: null },
} satisfies Meta<typeof Panel>;
export default meta;
type Story = StoryObj<typeof meta>;

function FilterPanel(args: Story["args"]) {
  const [cutoff, setCutoff] = useState(0.6);
  const [res, setRes] = useState(0.2);
  const [drive, setDrive] = useState(0.1);
  const [on, setOn] = useState(true);
  const [type, setType] = useState<string | null>("lp24");
  return (
    <Panel {...args} actions={<Toggle checked={on} onChange={setOn} label="on" />}>
      <Select
        value={type}
        onChange={setType}
        options={[
          { value: "lp24", label: "LP 24" },
          { value: "lp12", label: "LP 12" },
          { value: "hp12", label: "HP 12" },
        ]}
        label="Filter type"
      />
      <div className="flex gap-4">
        <Knob value={cutoff} onChange={setCutoff} label="Cutoff" format={(v) => `${Math.round(20 * Math.pow(1000, v))} Hz`} />
        <Knob value={res} onChange={setRes} label="Res" />
        <Knob value={drive} onChange={setDrive} label="Drive" size="sm" />
      </div>
    </Panel>
  );
}

export const Filter: Story = { render: (args) => <div className="w-72"><FilterPanel {...args} /></div> };
export const Tones: Story = {
  render: (args) => (
    <div className="grid grid-cols-3 gap-3">
      {(["osc", "filter", "env", "lfo", "fx", "master"] as const).map((t) => (
        <Panel key={t} {...args} title={t} tone={t}>
          <Knob value={0.5} onChange={() => {}} label="amt" size="sm" />
        </Panel>
      ))}
    </div>
  ),
};
export const Untitled: Story = { args: { title: undefined }, render: (args) => <Panel {...args}><span className="text-xs">no header</span></Panel> };
export const UntitledWithKnob: Story = {
  args: { title: undefined },
  render: (args) => (
    <Panel {...args}>
      <Knob value={0.5} onChange={() => {}} label="amt" />
    </Panel>
  ),
};

export const Switchable: Story = {
  render: (args) => {
    const [on, setOn] = useState(true);
    return (
      <Panel {...args} title="Filter" tone="filter" on={on} onToggle={setOn}>
        <span className="text-xs text-muted-foreground">Il LED nell'header accende e spegne la sezione.</span>
      </Panel>
    );
  },
};
