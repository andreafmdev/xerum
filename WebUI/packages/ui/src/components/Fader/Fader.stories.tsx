import { useState } from "react";
import type { Meta, StoryObj } from "@storybook/react-vite";
import { Fader, type FaderProps } from "./Fader";

const meta = {
  title: "Controls/Fader",
  component: Fader,
  args: { value: 0.6, label: "Level", onChange: () => {} },
  argTypes: { tone: { control: "select", options: ["osc", "filter", "env", "lfo", "fx", "master"] } },
} satisfies Meta<typeof Fader>;
export default meta;
type Story = StoryObj<typeof meta>;

function Controlled(props: FaderProps) {
  const [value, setValue] = useState(props.value);
  return <Fader {...props} value={value} onChange={setValue} />;
}

export const Vertical: Story = { render: (args) => <Controlled {...args} /> };
export const Horizontal: Story = { args: { orientation: "horizontal" }, render: (args) => <Controlled {...args} /> };
export const Tones: Story = {
  render: (args) => (
    <div className="flex gap-6">
      {(["osc", "filter", "env", "lfo", "fx", "master"] as const).map((t) => (
        <Controlled key={t} {...args} tone={t} label={t} />
      ))}
    </div>
  ),
};
export const Decibel: Story = {
  args: { tone: "master", format: (v) => `${(v * 12 - 6).toFixed(1)} dB`, defaultValue: 0.5 },
  render: (args) => <Controlled {...args} />,
};
export const Disabled: Story = { args: { disabled: true }, render: (args) => <Controlled {...args} /> };
