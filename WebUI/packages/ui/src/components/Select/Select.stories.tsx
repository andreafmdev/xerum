import { useState } from "react";
import type { Meta, StoryObj } from "@storybook/react-vite";
import { Select, type SelectProps } from "./Select";

const options = [
  { value: "basic", label: "Basic Shapes" },
  { value: "analog", label: "Analog Classics" },
  { value: "vocal", label: "Vocal Formants" },
  { value: "digital", label: "Digital Harsh" },
];

const meta = {
  title: "Controls/Select",
  component: Select,
  args: { value: "analog", options, label: "Wavetable", onChange: () => {} },
} satisfies Meta<typeof Select>;
export default meta;
type Story = StoryObj<typeof meta>;

function Controlled(props: SelectProps) {
  const [value, setValue] = useState(props.value);
  return (
    <div className="w-48">
      <Select {...props} value={value} onChange={setValue} />
    </div>
  );
}

export const Default: Story = { render: (args) => <Controlled {...args} /> };
export const Empty: Story = { args: { value: null, placeholder: "Choose table" }, render: (args) => <Controlled {...args} /> };
export const Disabled: Story = { args: { disabled: true }, render: (args) => <Controlled {...args} /> };
