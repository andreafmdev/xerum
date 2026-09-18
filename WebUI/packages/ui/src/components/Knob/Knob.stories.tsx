import { useState } from "react";
import type { Meta, StoryObj } from "@storybook/react-vite";
import { Knob, type KnobProps } from "./Knob";

const meta = {
  title: "Controls/Knob",
  component: Knob,
  args: { value: 0.35, label: "Cutoff", onChange: () => {} },
  argTypes: { tone: { control: "select", options: ["osc", "filter", "env", "lfo", "fx", "master"] } },
} satisfies Meta<typeof Knob>;
export default meta;
type Story = StoryObj<typeof meta>;

function Controlled(props: KnobProps) {
  const [value, setValue] = useState(props.value);
  return <Knob {...props} value={value} onChange={setValue} />;
}

export const Default: Story = { render: (args) => <Controlled {...args} /> };
export const Sizes: Story = {
  render: (args) => (
    <div className="flex items-end gap-6">
      <Controlled {...args} size="sm" label="sm" />
      <Controlled {...args} size="md" label="md" />
      <Controlled {...args} size="lg" label="lg" />
    </div>
  ),
};
export const Tones: Story = {
  render: (args) => (
    <div className="flex gap-6">
      {(["osc", "filter", "env", "lfo", "fx", "master"] as const).map((t) => (
        <Controlled key={t} {...args} tone={t} label={t} />
      ))}
    </div>
  ),
};
export const Bipolar: Story = {
  args: { value: 0.5, label: "Pan", bipolar: true, format: (v) => `${Math.round((v - 0.5) * 200)}` },
  render: (args) => <Controlled {...args} />,
};
export const Formatted: Story = {
  args: { value: 0.5, label: "Cutoff", tone: "filter", format: (v) => `${Math.round(20 * Math.pow(1000, v))} Hz` },
  render: (args) => <Controlled {...args} />,
};
export const Disabled: Story = { args: { disabled: true }, render: (args) => <Controlled {...args} /> };
export const Modulated: Story = {
  args: { value: 0.55, label: "Cutoff", tone: "filter" },
  render: (args) => (
    <div className="flex items-end gap-6">
      <Controlled {...args} mods={[{ tone: "lfo", depth: 0.2, bipolar: true }]} liveValue={0.62} />
      <Controlled {...args} label="Position" tone="osc" mods={[{ tone: "env", depth: 0.3 }]} liveValue={0.8} />
      <Controlled
        {...args}
        label="Both"
        mods={[
          { tone: "lfo", depth: 0.2, bipolar: true },
          { tone: "env", depth: 0.3 },
        ]}
        liveValue={0.7}
      />
    </div>
  ),
};
export const DropTarget: Story = {
  args: { value: 0.4, label: "Drop LFO here" },
  render: (args) => (
    <div className="flex items-end gap-6">
      <span
        draggable
        onDragStart={(e) => e.dataTransfer.setData("text/x-mod", "lfo")}
        className="cursor-grab rounded-full bg-lfo px-2 py-0.5 text-2xs font-semibold text-background"
      >
        LFO
      </span>
      <Controlled {...args} onDropMod={(src) => alert(`dropped ${src}`)} />
    </div>
  ),
};
