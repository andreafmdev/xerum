import { useCallback, useEffect, useRef } from "react";
import { toneStyle } from "@xerum/ui";
import { noteMaskOf } from "../../juce/backend";
import { sampleWave, spectrum } from "../curves";
import { formatValue } from "../mapping";
import { modsFor } from "../mod";
import { noteHz, topNote } from "../notes";
import { PARAM_SPECS } from "../params.generated";
import { useMeterFrame } from "./MetersContext";
import { useSynthCtx } from "./SynthContext";

type Props = {
  position: number;
  warp: number;
  level: number;
  name: string;
  /** Scala dello chassis. Serve solo al backing store del canvas: vedi `draw`. */
  scale: number;
};

const FRAMES = 9;
const HARMONICS = 32;

/** Schermo principale: pila di frame in prospettiva, spettro del frame corrente a destra. */
export function WaveDisplay({ position, warp, level, name, scale }: Props) {
  const ref = useRef<HTMLCanvasElement>(null);
  // Scostamento istantaneo della posizione: solo le sorgenti LFO assegnate a wtpos.
  const { mods } = useSynthCtx();
  const frame = useMeterFrame();
  const lfo = modsFor(mods, "wtpos").reduce((a, m) => a + (m.src === "lfo" ? m.depth * frame.lfo : 0), 0);
  // Una nota tenuta fa scorrere l'onda e le fa fare piu' cicli salendo di altezza; il "gate"
  // ingrossa e illumina i tratti mentre suona e si spegne dolcemente al rilascio. La nota si
  // legge dal mask del motore (la piu' acuta), non dalla tastiera a schermo: cosi' vale anche
  // per il MIDI dell'host.
  const top = topNote(noteMaskOf(frame));
  const hz = top === null ? 0 : noteHz(top);
  const hzRef = useRef(hz);
  hzRef.current = hz;
  const gate = useRef(0);

  const draw = useCallback(() => {
    const cv = ref.current;
    const ctx = cv?.getContext("2d");
    if (!cv || !ctx) return;
    const W = cv.clientWidth;
    const H = cv.clientHeight;
    // Lo chassis e' scalato con transform (SynthWindow.tsx), e transform non tocca il
    // backing store del canvas: senza moltiplicarlo per la scala, questo resterebbe a
    // risoluzione 1x mentre tutto il resto viene ingrandito — sfocato solo lo schermo
    // dell'onda. Il rapporto rect/clientWidth misura la scala davvero applicata (regge
    // anche se un giorno la si applicasse altrove); `scale` arriva come prop perche' serve
    // come *dipendenza*: e' l'unica cosa che dice a questo effetto di rigirare quando la
    // finestra cambia misura, e senza il canvas restava alla risoluzione del primo render.
    const rect = cv.getBoundingClientRect();
    const applied = W > 0 && rect.width > 0 ? rect.width / W : 1;
    const dpr = (window.devicePixelRatio || 1) * applied;
    const backingWidth = Math.round(W * dpr);
    if (cv.width !== backingWidth) {
      cv.width = backingWidth;
      cv.height = Math.round(H * dpr);
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

    // Il gate segue la nota con un attacco rapido e un rilascio in nove frame circa.
    const g = gate.current;
    gate.current = hzRef.current ? Math.min(1, g + 0.25) : g * 0.9;
    const t = performance.now() / 1000;
    const cyc = hzRef.current ? 1 + Math.min(5, Math.log2(hzRef.current / 55)) : 1;
    const scroll = hzRef.current ? (t * hzRef.current * 0.02) % 1 : 0;

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
        const y = dy - sampleWave(pos, ((i / 160) * cyc + scroll) % 1, warp) * amp * (0.55 + 0.45 * near) * (1 + gate.current * 0.25);
        if (i) ctx.lineTo(x, y);
        else ctx.moveTo(x, y);
      }
      ctx.lineWidth = 1 + near * 1.2 + gate.current * 0.6;
      ctx.strokeStyle = col;
      ctx.globalAlpha = 0.14 + near * 0.86;
      ctx.shadowBlur = near * (8 + gate.current * 10);
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
  }, [position, warp, level, lfo, scale]);

  // Il `resize` copre il caso rimanente: la finestra si sposta su uno schermo con un
  // devicePixelRatio diverso senza che la scala dello chassis cambi.
  useEffect(() => {
    draw();
    window.addEventListener("resize", draw);
    return () => window.removeEventListener("resize", draw);
  }, [draw]);

  // Finche' una nota suona (o il gate sta ancora scendendo) lo schermo si ridisegna a ogni
  // frame: e' l'unico momento in cui il tempo entra nel disegno. Con lo strumento fermo non
  // gira nessun requestAnimationFrame.
  useEffect(() => {
    if (!hz && gate.current < 0.02) return;
    let id = 0;
    const loop = () => {
      draw();
      if (hzRef.current || gate.current >= 0.02) id = requestAnimationFrame(loop);
    };
    id = requestAnimationFrame(loop);
    return () => cancelAnimationFrame(id);
  }, [hz, draw]);

  return (
    <div
      className="sx-display relative h-32.5 shrink-0 overflow-hidden rounded-plate bg-well shadow-well after:pointer-events-none after:absolute after:inset-0 after:bg-linear-to-br after:from-foreground/5 after:to-transparent"
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
