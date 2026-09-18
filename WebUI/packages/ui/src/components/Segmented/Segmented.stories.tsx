import { useState } from "react";
import type { Meta, StoryObj } from "@storybook/react-vite";
import { Segmented, type SegmentedProps } from "./Segmented";

const meta = {
  title: "Controls/Segmented",
  component: Segmented,
  args: {
    value: "lp",
    label: "Filter type",
    onChange: () => {},
    options: [
      { value: "lp", label: "LP" },
      { value: "hp", label: "HP" },
      { value: "bp", label: "BP" },
    ],
  },
  argTypes: { tone: { control: "select", options: ["osc", "filter", "env", "lfo", "fx", "master"] } },
} satisfies Meta<typeof Segmented>;
export default meta;
type Story = StoryObj<typeof meta>;

function Controlled(props: SegmentedProps) {
  const [value, setValue] = useState(props.value);
  return <Segmented {...props} value={value} onChange={setValue} />;
}

export const Default: Story = { render: (args) => <Controlled {...args} /> };
export const Toned: Story = { args: { tone: "filter" }, render: (args) => <Controlled {...args} /> };
export const Voices: Story = {
  args: {
    label: "Voice mode",
    value: "poly",
    tone: "master",
    options: [
      { value: "poly", label: "Poly" },
      { value: "mono", label: "Mono" },
      { value: "legato", label: "Legato" },
    ],
  },
  render: (args) => <Controlled {...args} />,
};
export const Disabled: Story = { args: { disabled: true }, render: (args) => <Controlled {...args} /> };
