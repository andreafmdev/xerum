import { useState } from "react";
import type { Meta, StoryObj } from "@storybook/react-vite";
import { Wheel, type WheelProps } from "./Wheel";

const meta = {
  title: "Controls/Wheel",
  component: Wheel,
  args: { value: 0.5, label: "Mod", onChange: () => {} },
  argTypes: { tone: { control: "select", options: ["osc", "filter", "env", "lfo", "fx", "master"] } },
} satisfies Meta<typeof Wheel>;
export default meta;
type Story = StoryObj<typeof meta>;

// La rotella cresce per riempire il genitore (flex-1): nella striscia reale l'altezza arriva dal
// contenitore della striscia, qui la diamo noi con h-fader per avere una storia leggibile.
function Controlled(props: WheelProps) {
  const [value, setValue] = useState(props.value);
  return (
    // `flex` e non solo l'altezza: la rotella e' `flex-1` dentro una colonna ad altezza auto, quindi
    // in un contenitore a blocco collasserebbe a zero e resterebbe visibile la sola etichetta.
    // Nella striscia reale (BottomStrip) il genitore e' gia' un flex con altezza definita.
    <div className="flex h-fader">
      <Wheel {...props} value={value} onChange={setValue} />
    </div>
  );
}

export const Pitch: Story = {
  args: { label: "Pitch", bipolar: true, springBack: true, defaultValue: 0.5 },
  render: (args) => <Controlled {...args} />,
};
export const Mod: Story = { render: (args) => <Controlled {...args} /> };
export const Tones: Story = {
  render: (args) => (
    <div className="flex gap-6">
      {(["osc", "filter", "env", "lfo", "fx", "master"] as const).map((t) => (
        <Controlled key={t} {...args} tone={t} label={t} />
      ))}
    </div>
  ),
};
export const Disabled: Story = { args: { disabled: true }, render: (args) => <Controlled {...args} /> };
