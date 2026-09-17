import { useState } from "react";
import type { Meta, StoryObj } from "@storybook/react-vite";
import { WavetableDisplay } from "./WavetableDisplay";
import { Knob } from "@/components/Knob/Knob";

const N = 128;
const FRAMES = 16;
/** Morph da sine a saw: frame i = mix. */
const frames = Array.from({ length: FRAMES }, (_, f) => {
  const mix = f / (FRAMES - 1);
  return Float32Array.from({ length: N }, (_, i) => {
    const t = i / N;
    const sine = Math.sin(t * Math.PI * 2);
    const saw = 2 * t - 1;
    return sine * (1 - mix) + saw * mix;
  });
});

const meta: Meta<typeof WavetableDisplay> = {
  title: "Display/WavetableDisplay",
  component: WavetableDisplay,
  args: { frames, position: 0.3, tone: "osc" },
  decorators: [(Story) => <div className="w-80"><Story /></div>],
};
export default meta;
type Story = StoryObj<typeof meta>;

export const Default: Story = {};
export const Empty: Story = { args: { frames: [] } };
export const WithPositionKnob: Story = {
  render: (args) => {
    const [pos, setPos] = useState(args.position);
    return (
      <div className="flex items-center gap-4">
        <WavetableDisplay {...args} position={pos} />
        <Knob value={pos} onChange={setPos} label="WT pos" />
      </div>
    );
  },
};
