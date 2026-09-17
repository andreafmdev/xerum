import { useState } from "react";
import type { Meta, StoryObj } from "@storybook/react-vite";
import { Tabs, type TabsProps } from "./Tabs";

const items = [
  { value: "osc", label: "OSC A" },
  { value: "osc-b", label: "OSC B" },
  { value: "noise", label: "Noise" },
  { value: "sub", label: "Sub" },
];

const meta = {
  title: "Layout/Tabs",
  component: Tabs,
  args: { value: "osc", items, onChange: () => {} },
} satisfies Meta<typeof Tabs>;
export default meta;
type Story = StoryObj<typeof meta>;

function Controlled(props: TabsProps) {
  const [value, setValue] = useState(props.value);
  return (
    <div className="flex flex-col gap-3">
      <Tabs {...props} value={value} onChange={setValue} />
      <p className="font-mono text-xs text-muted-foreground">active: {value}</p>
    </div>
  );
}

export const Default: Story = { render: (args) => <Controlled {...args} /> };
export const Toned: Story = { args: { tone: "filter" }, render: (args) => <Controlled {...args} /> };
