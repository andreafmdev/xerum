import { useEffect, useState } from "react";
import type { Meta, StoryObj } from "@storybook/react-vite";
import { Meter } from "./Meter";

const meta = {
  title: "Display/Meter",
  component: Meter,
  args: { level: 0.6, label: "Output", tone: "master" },
  argTypes: { tone: { control: "select", options: ["osc", "filter", "env", "lfo", "fx", "master"] } },
} satisfies Meta<typeof Meter>;
export default meta;
type Story = StoryObj<typeof meta>;

export const Default: Story = {};
export const Clipping: Story = { args: { level: 1 } };
export const Levels: Story = {
  render: (args) => (
    <div className="flex flex-col gap-2">
      {[0, 0.3, 0.6, 0.8, 1].map((l) => (
        <Meter key={l} {...args} level={l} label={`Level ${l}`} />
      ))}
    </div>
  ),
};
export const Animated: Story = {
  render: (args) => {
    const [t, setT] = useState(0);
    useEffect(() => {
      let id = 0;
      const tick = (now: number) => {
        setT(now / 1000);
        id = requestAnimationFrame(tick);
      };
      id = requestAnimationFrame(tick);
      return () => cancelAnimationFrame(id);
    }, []);
    return <Meter {...args} level={0.55 + 0.25 * Math.sin(t * 1.7) + 0.1 * Math.sin(t * 7.3)} />;
  },
};
