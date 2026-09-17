import { useState } from "react";
import type { Meta, StoryObj } from "@storybook/react-vite";
import { Toggle, type ToggleProps } from "./Toggle";

const meta = {
  title: "Controls/Toggle",
  component: Toggle,
  args: { checked: true, label: "Sync", onChange: () => {} },
} satisfies Meta<typeof Toggle>;
export default meta;
type Story = StoryObj<typeof meta>;

function Controlled(props: ToggleProps) {
  const [checked, setChecked] = useState(props.checked);
  return <Toggle {...props} checked={checked} onChange={setChecked} />;
}

export const Default: Story = { render: (args) => <Controlled {...args} /> };
export const Off: Story = { args: { checked: false }, render: (args) => <Controlled {...args} /> };
export const Tones: Story = {
  render: (args) => (
    <div className="flex flex-col gap-3">
      {(["osc", "filter", "env", "lfo", "fx", "master"] as const).map((t) => (
        <Controlled key={t} {...args} tone={t} label={t} />
      ))}
    </div>
  ),
};
export const Disabled: Story = { args: { disabled: true }, render: (args) => <Controlled {...args} /> };
