import { useCallback, useEffect, useRef, useState } from "react";
import { toneStyle } from "@xerum/ui";
import { noteMaskOf, type MeterFrame } from "../../juce/backend";
import { useChoiceParam, useFloatParam } from "../../juce/hooks";
import { useBackend } from "../../juce/provider";
import { canvas2d, cssColor } from "../canvas";
import { sampleWave, spectrum } from "../curves";
import { formatValue } from "../mapping";
import { modsFor } from "../mod";
import { noteHz, topNote } from "../notes";
import { PARAM_SPECS } from "../params.generated";
import { useSynthCtx } from "./SynthContext";

type Props = {
  /** Scala dello chassis. Serve solo al backing store del canvas: vedi `draw`. */
  scale: number;
};

const FRAMES = 9;
const HARMONICS = 32;

/** Il numero, o zero: un binario piu' vecchio della WebUI manda frame senza qualche campo. */
const finite = (v: number) => (typeof v === "number" && Number.isFinite(v) ? v : 0);

/** Schermo principale: pila di frame in prospettiva, spettro del frame corrente a destra. */
export function WaveDisplay({ scale }: Props) {
  const ref = useRef<HTMLCanvasElement>(null);
  // I parametri che disegnano l'onda si leggono qui e non in SynthWindow: trascinando wtpos
  // cambiano decine di volte al secondo e devono ridisegnare solo questo schermo, non la finestra.
  const position = useFloatParam("wtpos").value;
  const warp = useFloatParam("warp").value;
  const level = useFloatParam("level").value;
  const wt = useChoiceParam("wtIndex");
  const name = wt.options.find((o) => o.value === wt.value)?.label ?? "";

  // Scostamento istantaneo della posizione: solo le sorgenti LFO assegnate a wtpos.
  const { mods } = useSynthCtx();
  const modsRef = useRef(mods);
  modsRef.current = mods;

  // Cio' che arriva a 30 Hz dal motore sta in ref, non in stato: questo componente non deve
  // ri-renderizzare a ogni frame. Prima `lfo` era fra le dipendenze di `draw`, e ogni frame
  // rifaceva `draw`, staccava e riattaccava il listener di resize e riavviava il ciclo rAF.
  const lfoRef = useRef(0);
  // Una nota tenuta fa scorrere l'onda e le fa fare piu' cicli salendo di altezza; il "gate"
  // ingrossa e illumina i tratti mentre suona e si spegne dolcemente al rilascio. La nota si
  // legge dal mask del motore (la piu' acuta), non dalla tastiera a schermo: cosi' vale anche
  // per il MIDI dell'host.
  const hzRef = useRef(0);
  const gate = useRef(0);
  const looping = useRef(false);
  // L'unico stato React: "una nota suona". Cambia al note-on/off, non a ogni frame.
  const [playing, setPlaying] = useState(false);

  const draw = useCallback(() => {
    const cv = ref.current;
    // `scale` arriva come prop perche' serve come *dipendenza*: e' l'unica cosa che dice a
    // questo effetto di rigirare quando la finestra cambia misura, e senza il canvas restava
    // alla risoluzione del primo render (il backing store lo rifa' canvas2d).
    const c = canvas2d(cv);
    if (!cv || !c) return;
    const { ctx, W, H } = c;
    const col = cssColor(cv, "--tone");
    const grid = cssColor(cv, "--color-line-strong", col);

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

    const cur = Math.min(1, Math.max(0, position + 0.06 * lfoRef.current));
    const mags = spectrum(cur, warp, level, HARMONICS);
    const bw = (W * 0.32) / HARMONICS;
    ctx.fillStyle = col;
    ctx.globalAlpha = 0.14;
    mags.forEach((mag, h) => ctx.fillRect(W * 0.66 + h * bw, H - 6 - mag * (H - 30), bw - 1.5, mag * (H - 30)));
    ctx.globalAlpha = 1;

    // Il gate segue la nota con un attacco rapido e un rilascio in nove frame circa.
    const hz = hzRef.current;
    const g = gate.current;
    gate.current = hz ? Math.min(1, g + 0.25) : g * 0.9;
    const t = performance.now() / 1000;
    const cyc = hz ? 1 + Math.min(5, Math.log2(hz / 55)) : 1;
    const scroll = hz ? (t * hz * 0.02) % 1 : 0;

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
  }, [position, warp, level, scale]);
  // Il ciclo rAF e il listener dei meter durano piu' di un singolo `draw`: leggono sempre l'ultimo.
  const drawRef = useRef(draw);
  drawRef.current = draw;

  // Sottoscrizione diretta al backend invece di useMeterValue: quell'hook ri-renderizza quando
  // il valore cambia, e qui i due numeri finiscono in ref senza nessun render.
  const backend = useBackend();
  useEffect(
    () =>
      backend.onMeters((m: MeterFrame) => {
        const lfoLevel = finite(m.lfo);
        const lfo = modsFor(modsRef.current, "wtpos").reduce((a, x) => a + (x.src === "lfo" ? x.depth * lfoLevel : 0), 0);
        const moved = lfo !== lfoRef.current;
        lfoRef.current = lfo;
        const top = topNote(noteMaskOf({ ...m, n0: finite(m.n0), n1: finite(m.n1), n2: finite(m.n2), n3: finite(m.n3) }));
        hzRef.current = top === null ? 0 : noteHz(top);
        setPlaying(hzRef.current !== 0);
        // A strumento fermo l'LFO muove comunque la posizione: un disegno per frame, senza render.
        if (moved && !looping.current) drawRef.current();
      }),
    [backend],
  );

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
    if (!playing && gate.current < 0.02) return;
    looping.current = true;
    let id = 0;
    const loop = () => {
      drawRef.current();
      if (hzRef.current || gate.current >= 0.02) id = requestAnimationFrame(loop);
      else looping.current = false;
    };
    id = requestAnimationFrame(loop);
    return () => {
      cancelAnimationFrame(id);
      looping.current = false;
    };
  }, [playing]);

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
