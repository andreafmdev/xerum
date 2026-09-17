import type { Meta, StoryObj } from "@storybook/react-vite";
import { PlayIcon } from "lucide-react";
import { Button } from "./Button";

const meta = {
  title: "Controls/Button",
  component: Button,
  args: { children: "Randomize" },
} satisfies Meta<typeof Button>;
export default meta;
type Story = StoryObj<typeof meta>;

export const Default: Story = {};
export const Variants: Story = {
  render: (args) => (
    <div className="flex flex-wrap items-center gap-3">
      <Button {...args} variant="default" />
      <Button {...args} variant="secondary" />
      <Button {...args} variant="outline" />
      <Button {...args} variant="ghost" />
      <Button {...args} variant="destructive" />
    </div>
  ),
};
export const Sizes: Story = {
  render: (args) => (
    <div className="flex items-center gap-3">
      <Button {...args} size="xs" />
      <Button {...args} size="sm" />
      <Button {...args} size="default" />
      <Button {...args} size="icon" aria-label="Play"><PlayIcon /></Button>
    </div>
  ),
};
export const Tones: Story = {
  render: (args) => (
    <div className="flex flex-wrap gap-3">
      {(["osc", "filter", "env", "lfo", "fx", "master"] as const).map((t) => (
        <Button key={t} {...args} tone={t}>{t}</Button>
      ))}
    </div>
  ),
};
export const WithIcon: Story = {
  render: (args) => (
    <Button {...args}>
      <PlayIcon data-icon="inline-start" />
      Preview
    </Button>
  ),
};
