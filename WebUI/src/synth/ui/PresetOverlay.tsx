import { useState } from "react";
import { Button } from "@xerum/ui";
import { Search, X } from "lucide-react";
import { CATEGORIES, filterPresets, PRESETS, type Preset } from "../presets";
import { Logo } from "./Header";

type Props = { current: Preset; onPick: (p: Preset) => void; onClose: () => void };

const row = "flex items-center gap-2 rounded-control px-2.5 py-1.5 text-left text-[11px] text-muted-foreground transition-colors hover:bg-foreground/5 hover:text-foreground";

export function PresetOverlay({ current, onPick, onClose }: Props) {
  const [cat, setCat] = useState("All");
  const [q, setQ] = useState("");
  const list = filterPresets(PRESETS, cat, q);
  return (
    <div
      role="dialog"
      aria-label="Presets"
      onKeyDown={(e) => e.key === "Escape" && onClose()}
      className="absolute inset-0 z-20 flex animate-in flex-col gap-3 rounded-[14px] bg-background/92 p-3.5 backdrop-blur-sm fade-in zoom-in-[0.99] duration-150"
    >
      <div className="flex items-center gap-3">
        <Logo>PRESETS</Logo>
        <div className="flex h-7.5 flex-1 items-center gap-2 rounded-control bg-well px-2.5 font-mono text-xs text-text-dim shadow-well">
          <Search className="size-3.5" />
          <input type="search" autoFocus placeholder="Cerca preset…" value={q} onChange={(e) => setQ(e.target.value)} className="flex-1 bg-transparent text-foreground outline-none placeholder:text-text-dim" />
        </div>
        <Button variant="secondary" size="icon-xs" aria-label="Close presets" onClick={onClose} className="rounded-control! [&_svg]:size-3.5">
          <X />
        </Button>
      </div>
      <div className="grid min-h-0 flex-1 grid-cols-[150px_1fr] gap-3">
        <div className="flex flex-col gap-0.5">
          {CATEGORIES.map((c) => (
            <button key={c} type="button" onClick={() => setCat(c)} className={`${row} ${c === cat ? "bg-surface-2 text-foreground shadow-panel" : ""}`}>
              {c}
              <span className="ml-auto font-mono text-2xs text-text-dim">{c === "All" ? PRESETS.length : PRESETS.filter((p) => p.cat === c).length}</span>
            </button>
          ))}
        </div>
        <div className="grid content-start grid-cols-3 gap-[3px] overflow-auto pr-1">
          {list.map((p) => (
            <button
              key={p.name}
              type="button"
              onClick={() => onPick(p)}
              className={`${row} h-8.5 ${p.name === current.name ? "bg-surface-1 text-osc shadow-[inset_0_0_0_1px_color-mix(in_oklch,var(--color-osc)_40%,transparent)]" : ""}`}
            >
              {p.name}
              <span className="ml-auto text-[9px] tracking-wider text-text-dim uppercase">{p.cat}</span>
            </button>
          ))}
        </div>
      </div>
    </div>
  );
}
