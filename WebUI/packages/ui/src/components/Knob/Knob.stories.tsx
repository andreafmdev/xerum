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
