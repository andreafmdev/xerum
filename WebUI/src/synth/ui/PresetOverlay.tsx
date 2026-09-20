import { useEffect, useRef, useState } from "react";
import { Button } from "@xerum/ui";
import { Search, X } from "lucide-react";
import { canvas2d, cssColor } from "../canvas";
import { tableSample } from "../curves";
import { AUTHOR, BANK, CATEGORIES, DESCRIPTIONS, filterPresets, presetWave, PRESETS, type Preset } from "../presets";
import { Logo } from "./Header";

type Props = { current: Preset; onPick: (p: Preset) => void; onClose: () => void };

// Preset per categoria, contati una volta: nel render girava un filter per categoria a ogni
// tasto battuto nella ricerca.
const COUNT_BY_CAT = new Map<string, number>();
for (const p of PRESETS) COUNT_BY_CAT.set(p.cat, (COUNT_BY_CAT.get(p.cat) ?? 0) + 1);

const catRow =
  "sx-cat flex items-center gap-2 rounded-control px-2.5 py-1.5 text-left text-[11px] text-muted-foreground transition-colors hover:bg-foreground/5 hover:text-foreground data-[on=true]:bg-surface-2 data-[on=true]:text-foreground data-[on=true]:shadow-panel";

/**
 * Il browser dei preset: categorie a sinistra, griglia di schede al centro, anteprima a destra.
 *
 * Un clic sulla scheda la *seleziona* e la porta nell'anteprima; il caricamento e' un gesto a
 * parte (il tasto "Carica preset" o un doppio clic), perche' cambiare preset rimpiazza tutti i
 * parametri e non deve succedere mentre si sfoglia.
 */
export function PresetOverlay({ current, onPick, onClose }: Props) {
  const [cat, setCat] = useState("All");
  const [q, setQ] = useState("");
  const [sel, setSel] = useState<Preset>(current);
  const list = filterPresets(PRESETS, cat, q);
  const selWave = presetWave(sel);
  const loaded = sel.name === current.name;
  return (
    <div
      role="dialog"
      aria-label="Presets"
      onKeyDown={(e) => e.key === "Escape" && onClose()}
      className="sx-ovl absolute inset-0 z-20 flex animate-in flex-col gap-3 rounded-[14px] bg-background/94 p-3.5 backdrop-blur-sm fade-in zoom-in-[0.99] duration-150"
    >
      <div className="flex items-center gap-3">
        <Logo>PRESETS</Logo>
        <div className="sx-search flex h-7.5 flex-1 items-center gap-2 rounded-control bg-well px-2.5 font-mono text-xs text-text-dim shadow-well">
          <Search className="size-3.5" />
          <input type="search" autoFocus placeholder="Cerca preset…" value={q} onChange={(e) => setQ(e.target.value)} className="flex-1 bg-transparent text-foreground outline-none placeholder:text-text-dim" />
        </div>
        <Button variant="secondary" size="icon-xs" aria-label="Close presets" onClick={onClose} className="sx-hbtn rounded-control! [&_svg]:size-3.5">
          <X />
        </Button>
      </div>

      <div className="grid min-h-0 flex-1 grid-cols-[130px_1fr_240px] gap-3">
        <div className="flex flex-col gap-0.5">
          {CATEGORIES.map((c) => (
            <button key={c} type="button" data-on={c === cat} onClick={() => setCat(c)} className={catRow}>
              {c}
              <span className="ml-auto font-mono text-2xs text-text-dim">{c === "All" ? PRESETS.length : (COUNT_BY_CAT.get(c) ?? 0)}</span>
            </button>
          ))}
        </div>

        <div className="grid content-start grid-cols-3 gap-2.5 overflow-auto p-0.5 pr-1.5">
          {list.map((p) => {
            const w = presetWave(p);
            const selected = p.name === sel.name;
            const isCurrent = p.name === current.name;
            return (
              <button
                key={p.name}
                type="button"
                data-selected={selected}
                data-current={isCurrent}
                aria-pressed={selected}
                onClick={() => setSel(p)}
                onDoubleClick={() => onPick(p)}
                className={`sx-card relative flex flex-col gap-1.5 rounded-[10px] bg-linear-to-b from-surface-2 to-surface-1 p-2 text-left shadow-panel transition-transform hover:-translate-y-px ${selected ? "shadow-[var(--shadow-panel),inset_0_0_0_1px_color-mix(in_oklch,var(--color-osc)_60%,transparent),0_0_16px_color-mix(in_oklch,var(--color-osc)_25%,transparent)]" : ""}`}
              >
                <div className="sx-wv relative h-16 overflow-hidden rounded-control bg-well shadow-well">
                  <MiniWave pos={w.pos} warp={w.warp} frames={w.frames} />
                </div>
                <div className="text-xs font-semibold tracking-[0.01em] text-foreground">
                  {p.name}
                  {isCurrent && <i aria-hidden className="ml-1.5 inline-block size-[5px] rounded-full bg-osc align-middle shadow-[0_0_6px_var(--color-osc)]" />}
                </div>
                <div className="flex justify-between text-[9px] text-text-dim">
                  <span>{AUTHOR}</span>
                  <span className="tracking-wider uppercase">{p.cat}</span>
                </div>
              </button>
            );
          })}
        </div>

        <div className="sx-prev relative flex flex-col gap-2.5 rounded-lg bg-linear-to-b from-surface-2 to-surface-1 p-3 shadow-panel">
          <div className="sx-wv relative h-30 overflow-hidden rounded-control bg-well shadow-well">
            <MiniWave pos={selWave.pos} warp={selWave.warp} frames={selWave.frames} />
          </div>
          <h4 data-testid="preset-preview-name" className="m-0 text-[15px] font-semibold tracking-[0.01em]">{sel.name}</h4>
          <dl className="grid grid-cols-[auto_1fr] gap-x-3 gap-y-1 text-2xs text-muted-foreground">
            <dt className="text-[9px] font-medium tracking-wider text-text-dim uppercase">Banca</dt>
            <dd>{BANK}</dd>
            <dt className="text-[9px] font-medium tracking-wider text-text-dim uppercase">Autore</dt>
            <dd>{AUTHOR}</dd>
            <dt className="text-[9px] font-medium tracking-wider text-text-dim uppercase">Tipo</dt>
            <dd>{sel.cat}</dd>
          </dl>
          <p className="m-0 text-[11px] leading-relaxed text-pretty text-muted-foreground">{DESCRIPTIONS[sel.cat] ?? ""}</p>
          <button
            type="button"
            onClick={() => onPick(sel)}
            className="mt-auto h-7.5 rounded-control bg-linear-to-r from-osc to-lfo text-[11px] font-semibold tracking-[0.14em] text-background uppercase shadow-[0_4px_12px_color-mix(in_oklch,var(--color-osc)_30%,transparent)] transition-[transform,filter] hover:brightness-110 active:translate-y-px"
          >
            {loaded ? "Caricato" : "Carica preset"}
          </button>
        </div>
      </div>
    </div>
  );
}

const MINI_FRAMES = 8;

/** Miniatura dell'onda: la pila di frame dello schermo principale, in piccolo e senza spettro. */
function MiniWave({ pos, warp, frames }: { pos: number; warp: number; frames: number[][] }) {
  const ref = useRef<HTMLCanvasElement>(null);
  useEffect(() => {
    const cv = ref.current;
    // Il browser sta sopra lo chassis scalato: stesso backing store dello schermo principale.
    const c = canvas2d(cv);
    if (!cv || !c) return;
    const { ctx, W, H } = c;
    const col = cssColor(cv, "--color-osc");
    const x0 = 14;
    const w0 = W * 0.62;
    const amp = (H - 40) / 2;
    for (let f = 0; f < MINI_FRAMES; f++) {
      const p = f / (MINI_FRAMES - 1);
      const dx = x0 + p * (W * 0.26);
      const dy = H / 2 + 10 - p * 20;
      const w = w0 - p * 26;
      const near = Math.max(0, 1 - Math.abs(p - pos) * 3.5);
      ctx.beginPath();
      for (let i = 0; i <= 96; i++) {
        const x = dx + (i / 96) * w;
        const y = dy - tableSample(frames, p, i / 96, warp) * amp * (0.5 + 0.5 * near);
        if (i) ctx.lineTo(x, y);
        else ctx.moveTo(x, y);
      }
      ctx.strokeStyle = col;
      ctx.globalAlpha = 0.15 + near * 0.85;
      ctx.lineWidth = 1 + near;
      ctx.shadowBlur = near * 8;
      ctx.shadowColor = col;
      ctx.stroke();
    }
    ctx.globalAlpha = 1;
    ctx.shadowBlur = 0;
  }, [pos, warp, frames]);
  return <canvas ref={ref} aria-hidden className="absolute inset-0 size-full" />;
}
