import type { Meta, StoryObj } from "@storybook/react-vite";
import { SectionHeader } from "./SectionHeader";
import { Toggle } from "@/components/Toggle/Toggle";

const meta: Meta<typeof SectionHeader> = {
  title: "Layout/SectionHeader",
  component: SectionHeader,
  args: { title: "Filter", tone: "filter" },
  decorators: [(Story) => <div className="w-80"><Story /></div>],
};
export default meta;
type Story = StoryObj<typeof meta>;

export const Default: Story = {};
export const WithActions: Story = {
  args: { title: "LFO 1", tone: "lfo", actions: <Toggle checked onChange={() => {}} label="sync" /> },
};
