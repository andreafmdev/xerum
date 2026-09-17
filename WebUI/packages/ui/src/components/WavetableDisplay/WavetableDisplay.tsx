import { useEffect, useRef } from "react";
import { cn } from "@/lib/utils";
import { toneStyle, type Tone } from "@/lib/tone";

export type WavetableDisplayProps = {
  /** Frame della wavetable, ciascuno campioni in -1..1. */
  frames: Float32Array[];
  /** Posizione 0..1 nella tabella. */
  position: number;
  tone?: Tone;
  className?: string;
};

const NEIGHBOURS = 3;

export function frameIndex(position: number, count: number): number {
  if (count <= 0) return 0;
  const p = Math.min(1, Math.max(0, position));
  return Math.round(p * (count - 1));
}

function drawFrame(
  ctx: CanvasRenderingContext2D,
  frame: Float32Array | null,
  w: number,
  h: number,
  offset: number,
  alpha: number,
  color: string,
) {
  const inset = 6;
  const midY = h / 2 - offset * 4;
  const amp = (h / 2 - inset) * (1 - offset * 0.12);
  ctx.beginPath();
  if (!frame || frame.length === 0) {
    ctx.moveTo(inset, midY);
    ctx.lineTo(w - inset, midY);
  } else {
    const n = frame.length;
    for (let i = 0; i < n; i++) {
      const x = inset + offset * 6 + ((w - inset * 2 - offset * 12) * i) / (n - 1);
      const y = midY - (frame[i] ?? 0) * amp;
      if (i === 0) ctx.moveTo(x, y);
      else ctx.lineTo(x, y);
    }
  }
  ctx.globalAlpha = alpha;
  ctx.strokeStyle = color;
  ctx.lineWidth = offset === 0 ? 2 : 1;
  ctx.stroke();
}

export function WavetableDisplay({ frames, position, tone, className }: WavetableDisplayProps) {
  const canvasRef = useRef<HTMLCanvasElement>(null);

  useEffect(() => {
    const canvas = canvasRef.current;
    if (!canvas) return;

    const draw = () => {
      const ctx = canvas.getContext("2d");
      if (!ctx) return;
      const dpr = window.devicePixelRatio || 1;
      const w = canvas.clientWidth || 240;
      const h = canvas.clientHeight || 96;
      canvas.width = Math.round(w * dpr);
      canvas.height = Math.round(h * dpr);
      ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
      ctx.clearRect(0, 0, w, h);

      let color = "currentColor";
      try {
        color = getComputedStyle(canvas).getPropertyValue("--tone").trim() || "currentColor";
      } catch {
        color = "currentColor";
      }
      const current = frameIndex(position, frames.length);

      if (frames.length === 0) {
        drawFrame(ctx, null, w, h, 0, 0.6, color);
        return;
      }
      // Vicini più lontani prima (dietro), poi il corrente sopra.
      for (let d = NEIGHBOURS; d >= 1; d--) {
        const back = current - d;
        const fwd = current + d;
        if (fwd < frames.length) drawFrame(ctx, frames[fwd] ?? null, w, h, d, 0.35 / d, color);
        if (back >= 0) drawFrame(ctx, frames[back] ?? null, w, h, -d, 0.35 / d, color);
      }
      drawFrame(ctx, frames[current] ?? null, w, h, 0, 1, color);
    };

    draw();
    const ro = new ResizeObserver(draw);
    ro.observe(canvas);
    return () => ro.disconnect();
  }, [frames, position]);

  return (
    <div
      data-testid="wavetable"
      data-slot="wavetable-display"
      className={cn("relative h-24 w-full overflow-hidden rounded-md bg-surface-0 ring-1 ring-border", className)}
      style={toneStyle(tone)}
    >
      <canvas ref={canvasRef} role="img" aria-label="Wavetable" className="size-full text-(--tone)" />
    </div>
  );
}
