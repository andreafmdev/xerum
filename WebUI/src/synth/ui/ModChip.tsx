import { MOD_DRAG_TYPE, toneStyle } from "@xerum/ui";
import { SOURCE_LABEL, SOURCE_TONE, type ModSource } from "../mod";

/** Chip trascinabile di una sorgente: si lascia cadere su un Knob per assegnarla. */
export function ModChip({ src }: { src: ModSource }) {
  return (
    <span
      draggable
      title="Trascina su un knob per modulare"
      data-testid={`mod-chip-${src}`}
      style={toneStyle(SOURCE_TONE[src])}
      onDragStart={(e) => {
        e.dataTransfer.setData(MOD_DRAG_TYPE, src);
        e.dataTransfer.effectAllowed = "copy";
      }}
      className="inline-flex h-4.5 cursor-grab items-center gap-1.5 rounded-full border border-edge-dark bg-linear-to-b from-cap-hi to-cap-lo pr-2 pl-1.5 text-2xs font-semibold tracking-wider text-(--tone) shadow-cap transition-transform hover:-translate-y-px active:cursor-grabbing [text-shadow:var(--tglow)]"
    >
      <i className="size-1.5 rounded-full bg-(--tone) shadow-[0_0_5px_var(--tone)]" />
      {SOURCE_LABEL[src]}
    </span>
  );
}
