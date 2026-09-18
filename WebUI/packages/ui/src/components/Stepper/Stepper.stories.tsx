import { useState } from "react";
import type { Meta, StoryObj } from "@storybook/react-vite";
import { Stepper, type StepperProps } from "./Stepper";

const signed = (v: number) => (v > 0 ? `+${v}` : `${v}`);

const meta = {
  title: "Controls/Stepper",
  component: Stepper,
  args: { value: 0, min: -3, max: 3, label: "Octave", unit: "OCT", format: signed, onChange: () => {} },
  argTypes: { tone: { control: "select", options: ["osc", "filter", "env", "lfo", "fx", "master"] } },
} satisfies Meta<typeof Stepper>;
export default meta;
type Story = StoryObj<typeof meta>;

function Controlled(props: StepperProps) {
  const [value, setValue] = useState(props.value);
  return <Stepper {...props} value={value} onChange={setValue} />;
}

export const Default: Story = { render: (args) => <Controlled {...args} /> };
export const Semitones: Story = {
  args: { label: "Semitones", unit: "SEMI", min: -12, max: 12, value: 7, tone: "osc" },
  render: (args) => <Controlled {...args} />,
};
export const Stack: Story = {
  render: (args) => (
    <div className="flex flex-col gap-1.5">
      <Controlled {...args} />
      <Controlled {...args} label="Semitones" unit="SEMI" min={-12} max={12} />
    </div>
  ),
};
export const Disabled: Story = { args: { disabled: true }, render: (args) => <Controlled {...args} /> };
