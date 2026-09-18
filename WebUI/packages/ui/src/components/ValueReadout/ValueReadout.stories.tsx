import type { Meta, StoryObj } from "@storybook/react-vite";
import { ValueReadout } from "./ValueReadout";

const meta = {
  title: "Display/ValueReadout",
  component: ValueReadout,
  args: { value: "440.00 Hz" },
} satisfies Meta<typeof ValueReadout>;
export default meta;
type Story = StoryObj<typeof meta>;

export const Default: Story = {};
export const Labelled: Story = { args: { label: "Cutoff", value: "2.4 kHz" } };
export const Proportional: Story = { args: { value: "Sine", mono: false } };
