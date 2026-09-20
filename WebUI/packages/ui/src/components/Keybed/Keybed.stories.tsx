import { useEffect, useRef } from "react";
import type { Meta, StoryObj } from "@storybook/react-vite";
import { Keybed, type KeybedNoteMask } from "./Keybed";

// C2 in convenzione MIDI standard (C4 = nota 60, il Do centrale).
const C2 = 36;

type NotesCallback = (mask: KeybedNoteMask) => void;

const meta: Meta<typeof Keybed> = {
  title: "Controls/Keybed",
  component: Keybed,
  args: {
    firstNote: C2,
    octaves: 4,
    velocity: 0.8,
    onNoteOn: () => {},
    onNoteOff: () => {},
    onAllNotesOff: () => {},
  },
  decorators: [(Story) => <div className="h-32 w-full"><Story /></div>],
};
export default meta;
type Story = StoryObj<typeof meta>;

export const Default: Story = {};

/**
 * Simula il mask che arriverebbe dall'host a 30fps: fa scorrere un piccolo cluster di note
 * accese avanti e indietro sulla tastiera, per verificare a occhio l'accensione via subscribeNotes
 * senza toccare React (nessun setState nel percorso, come da specifica).
 */
export const Playing: Story = {
  render: (args) => {
    const notesRef = useRef<NotesCallback | null>(null);

    const subscribeNotes = (cb: NotesCallback) => {
      notesRef.current = cb;
      return () => { notesRef.current = null; };
    };

    useEffect(() => {
      let frame = 0;
      const id = setInterval(() => {
        frame++;
        const base = C2 + 12 + (frame % 24);
        const mask: [number, number, number, number] = [0, 0, 0, 0];
        for (const note of [base, base + 4, base + 7]) {
          const word = note >> 5;
          if (word >= 0 && word < 4) mask[word] |= 1 << (note & 31);
        }
        notesRef.current?.(mask);
      }, 1000 / 30);
      return () => clearInterval(id);
    }, []);

    return <Keybed {...args} subscribeNotes={subscribeNotes} />;
  },
};
