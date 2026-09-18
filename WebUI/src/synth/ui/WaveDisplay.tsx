import { useEffect, useRef } from "react";
import { toneStyle } from "@xerum/ui";
import { sampleWave, spectrum } from "../curves";
import { formatValue } from "../mapping";
import { PARAM_SPECS } from "../params.generated";

type Props = { position: number; warp: number; level: number; lfo: number; name: string };

const FRAMES = 9;
const HARMONICS = 32;

/** Schermo principale: pila di frame in prospettiva, spettro del frame corrente a destra. */
export function WaveDisplay({ position, warp, level, lfo, name }: Props) {
  const ref = useRef<HTMLCanvasElement>(null);

  useEffect(() => {
    const cv = ref.current;
    const ctx = cv?.getContext("2d");
    if (!cv || !ctx) return;
    const W = cv.clientWidth;
    const H = cv.clientHeight;
    const dpr = window.devicePixelRatio || 1;
    if (cv.width !== W * dpr) {
      cv.width = W * dpr;
      cv.height = H * dpr;
    }
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
    ctx.clearRect(0, 0, W, H);
    const col = getComputedStyle(cv).getPropertyValue("--tone").trim() || "currentColor";
    const grid = getComputedStyle(cv).getPropertyValue("--color-line-strong").trim() || col;

    ctx.strokeStyle = grid;
    ctx.globalAlpha = 0.18;
    ctx.lineWidth = 1;
    for (let x = 0; x < W; x += 30) {
      ctx.beginPath();
      ctx.moveTo(x, 0);
      ctx.lineTo(x, H);
      ctx.stroke();
    }
    ctx.globalAlpha = 1;

    const cur = Math.min(1, Math.max(0, position + 0.06 * lfo));
    const mags = spectrum(cur, warp, level, HARMONICS);
    const bw = (W * 0.32) / HARMONICS;
    ctx.fillStyle = col;
    ctx.globalAlpha = 0.14;
    mags.forEach((mag, h) => ctx.fillRect(W * 0.66 + h * bw, H - 6 - mag * (H - 30), bw - 1.5, mag * (H - 30)));
    ctx.globalAlpha = 1;

    const x0 = 26;
    const w0 = W * 0.55;
    const amp = ((H - 44) / 2) * level;
    for (let f = 0; f < FRAMES; f++) {
      const pos = f / (FRAMES - 1);
      const depth = pos;
      const dx = x0 + depth * 46;
      const dy = H / 2 + 10 - depth * 22;
      const w = w0 - depth * 36;
      const near = Math.max(0, 1 - Math.abs(pos - cur) * 4);
      ctx.beginPath();
      for (let i = 0; i <= 160; i++) {
        const x = dx + (i / 160) * w;
        const y = dy - sampleWave(pos, i / 160, warp) * amp * (0.55 + 0.45 * near);
        if (i) ctx.lineTo(x, y);
        else ctx.moveTo(x, y);
      }
      ctx.lineWidth = 1 + near * 1.2;
      ctx.strokeStyle = col;
      ctx.globalAlpha = 0.14 + near * 0.86;
      ctx.shadowBlur = near * 8;
      ctx.shadowColor = col;
      ctx.stroke();
      ctx.shadowBlur = 0;
    }
    ctx.globalAlpha = 1;
    const px = x0 + cur * 46 + (w0 - cur * 36) / 2;
    const py = H / 2 + 10 - cur * 22;
    ctx.fillStyle = col;
    ctx.beginPath();
    ctx.arc(px, py + amp * 0.6 + 8, 2, 0, 7);
    ctx.fill();
  }, [position, warp, level, lfo]);

  return (
    <div
      className="relative h-32.5 shrink-0 overflow-hidden rounded-plate bg-well shadow-well after:pointer-events-none after:absolute after:inset-0 after:bg-linear-to-br after:from-foreground/5 after:to-transparent"
      style={toneStyle("osc")}
    >
      <canvas ref={ref} role="img" aria-label="Wavetable display" className="absolute inset-0 size-full" />
      <div className="absolute top-2 left-3 flex gap-3.5 font-mono text-2xs tracking-wider text-text-dim uppercase">
        <span>
          Wavetable <b className="font-medium text-(--tone) [text-shadow:var(--tglow)]">{name}</b>
        </span>
        <span>
          Frame <b className="font-medium text-(--tone) [text-shadow:var(--tglow)]">{formatValue(PARAM_SPECS.wtpos, position).split(".")[0]!.padStart(2, "0")}</b>/64
        </span>
      </div>
      <div className="absolute top-2 right-3 font-mono text-2xs tracking-wider text-text-dim">SPECTRUM · {HARMONICS} h</div>
    </div>
  );
}
