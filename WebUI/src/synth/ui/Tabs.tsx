import { useMemo, type ReactNode } from "react";
import { Segmented, Tabs, Toggle, toneStyle } from "@xerum/ui";
import { X } from "lucide-react";
import { envPath, lfoPath } from "../curves";
import { fmt } from "../format";
import { lfoShape, MOD_SOURCES, SOURCE_LABEL, SOURCE_TONE, type ModAssignment } from "../mod";
import { LABELS, type ArpMode, type LfoShape } from "../params";
import { ModChip } from "./ModChip";
import { ParamKnob, type SynthKnobCtx } from "./ParamKnob";
import type { TabId } from "../useSynth";

const TAB_ITEMS = [
  { value: "env", label: "Envelope", tone: "env" },
  { value: "lfo", label: "LFO", tone: "lfo" },
  { value: "mod", label: "Mod matrix", tone: "lfo" },
  { value: "fx", label: "Effects", tone: "fx" },
  { value: "arp", label: "Arpeggiator", tone: "master" },
] as const;

export function TabArea({ tab, setTab, children }: { tab: TabId; setTab: (t: TabId) => void; children: ReactNode }) {
  const tone = TAB_ITEMS.find((t) => t.value === tab)!.tone;
  return (
    <section className="sx-plate flex h-31 shrink-0 flex-col overflow-hidden rounded-plate shadow-panel" style={toneStyle(tone)}>
      <div className="flex h-7 shrink-0 items-stretch bg-surface-0 shadow-[inset_0_-1px_0_var(--color-edge-dark)]">
        <Tabs variant="bar" value={tab} onChange={(v) => setTab(v as TabId)} items={[...TAB_ITEMS]} className="flex-1" />
        <div className="flex items-center gap-1.5 px-2.5 text-[9px] tracking-[0.14em] text-text-dim uppercase">
          Mod sources
          {MOD_SOURCES.map((s) => (
            <ModChip key={s} src={s} />
          ))}
        </div>
      </div>
      {children}
    </section>
  );
}

const content = "flex flex-1 items-center gap-4 px-3.5 pt-1.5 pb-2";
const group = "flex items-end gap-2.5";
const vsep = "w-px self-stretch bg-linear-to-b from-transparent via-edge-dark to-transparent";
const screen = "shrink-0 overflow-hidden rounded-control bg-well shadow-well";

export function EnvTab({ ctx }: { ctx: SynthKnobCtx }) {
  const { p } = ctx;
  const W = 200;
  const H = 66;
  const d = useMemo(() => envPath(p.att, p.dec, p.sus, p.rel, W, H), [p.att, p.dec, p.sus, p.rel]);
  return (
    <div className={content}>
      <div className={screen} style={{ width: W, height: H }}>
        <svg width={W} height={H}>
          <path d={`${d} L${W - 6} ${H - 6} L6 ${H - 6}Z`} className="fill-(--tone)" opacity={0.12} />
          <path d={d} className="fill-none stroke-(--tone) [filter:var(--glow)]" strokeWidth={1.8} />
        </svg>
      </div>
      <div className={`${group} gap-3.5`}>
        <ParamKnob id="att" ctx={ctx} label="Attack" format={fmt.envMs} />
        <ParamKnob id="dec" ctx={ctx} label="Decay" format={fmt.envMs} />
        <ParamKnob id="sus" ctx={ctx} label="Sustain" format={fmt.pct} />
        <ParamKnob id="rel" ctx={ctx} label="Release" format={fmt.envMs} />
      </div>
      <div className={vsep} />
      <div className={group}>
        <ParamKnob id="envVel" ctx={ctx} label="Vel → amp" size="sm" format={fmt.pct} />
        <ParamKnob id="envCurve" ctx={ctx} label="Curve" size="sm" bipolar format={fmt.curve} />
      </div>
      <div className={vsep} />
      <p className="max-w-38 text-[11px] leading-snug text-text-dim">
        Trascina il chip <b className="text-env">ENV</b> su un knob per assegnarlo come sorgente.
      </p>
    </div>
  );
}

const SHAPES: { value: LfoShape; label: string }[] = (["Sine", "Tri", "Saw", "Square", "S&H"] as const).map((s) => ({ value: s, label: s }));

export function LfoTab({ ctx, phase }: { ctx: SynthKnobCtx; phase: number }) {
  const { p, set } = ctx;
  const W = 200;
  const H = 66;
  const d = useMemo(() => lfoPath(p.lshape, W, H), [p.lshape]);
  const cx = 6 + (((phase * 2) % 2) / 2) * (W - 12);
  const cy = H / 2 - lfoShape(p.lshape, phase * 2) * (H / 2 - 8);
  return (
    <div className={content}>
      <div className={screen} style={{ width: W, height: H }}>
        <svg width={W} height={H}>
          <line x1="6" x2={W - 6} y1={H / 2} y2={H / 2} className="stroke-line-strong" opacity={0.4} />
          <path d={d} className="fill-none stroke-(--tone) [filter:var(--glow)]" strokeWidth={1.6} />
          <circle cx={cx} cy={cy} r="3.5" className="fill-foreground" />
        </svg>
      </div>
      <div className="flex flex-col gap-2">
        <Segmented label="LFO shape" value={p.lshape} onChange={(v) => set("lshape", v)} options={SHAPES} />
        <div className="flex items-center gap-2.5">
          <Toggle checked={p.lsync} onChange={(v) => set("lsync", v)} label="Sync" />
          <Toggle checked={p.lretrig} onChange={(v) => set("lretrig", v)} label="Retrig" />
        </div>
      </div>
      <div className={`${group} gap-3.5`}>
        <ParamKnob id="lrate" ctx={ctx} label="Rate" format={(v) => fmt.lfoRate(v, p.lsync)} />
        <ParamKnob id="lphase" ctx={ctx} label="Phase" format={fmt.deg} />
        <ParamKnob id="lfade" ctx={ctx} label="Fade in" format={fmt.fadeMs} />
      </div>
    </div>
  );
}

export function ModTab({ mods, setDepth, remove }: { mods: ModAssignment[]; setDepth: (i: number, d: number) => void; remove: (i: number) => void }) {
  if (!mods.length) {
    return (
      <div className={content}>
        <p className="text-[11px] leading-snug text-text-dim">
          Nessuna assegnazione. Trascina <b className="text-lfo">LFO</b>, <b className="text-env">ENV</b>, <b className="text-master">VEL</b> o{" "}
          <b className="text-filter">MW</b> dalla barra dei tab su un qualsiasi knob.
        </p>
      </div>
    );
  }
  return (
    <div className="grid flex-1 grid-cols-2 content-start gap-x-6 px-3.5 py-1.5">
      {mods.map((m, i) => {
        const name = `${SOURCE_LABEL[m.src]} → ${LABELS[m.target]}`;
        return (
          <div key={`${m.src}-${m.target}`} className="flex h-5.5 items-center gap-2.5 text-[10px]" style={toneStyle(SOURCE_TONE[m.src])}>
            <span className="w-8 font-semibold tracking-wider text-(--tone)">{SOURCE_LABEL[m.src]}</span>
            <span className="text-text-dim">→</span>
            <span className="w-22 truncate text-muted-foreground">{LABELS[m.target]}</span>
            <input type="range" min="-1" max="1" step="0.01" value={m.depth} aria-label={`Depth ${name}`} onChange={(e) => setDepth(i, +e.target.value)} className="sx-range" />
            <span className="w-8 text-right font-mono text-foreground tabular-nums">{fmt.signedInt(Math.round(m.depth * 100))}</span>
            <button type="button" aria-label={`Remove ${name}`} onClick={() => remove(i)} className="text-text-dim hover:text-destructive">
              <X className="size-3" />
            </button>
          </div>
        );
      })}
    </div>
  );
}

export function FxTab({ ctx }: { ctx: SynthKnobCtx }) {
  const { p, set } = ctx;
  const slot = (id: "fx1On" | "fx2On", name: string, knobs: ReactNode) => (
    <div className={`flex flex-1 items-center gap-2.5 rounded-control bg-surface-1 px-2.5 py-1 shadow-[inset_0_0_0_1px_var(--color-edge-dark),inset_0_1px_0_var(--color-edge-light)] ${p[id] ? "" : "[&_.fx-nm]:opacity-45 [&_[data-slot=knob]]:opacity-45"}`}>
      <Toggle checked={p[id]} onChange={(v) => set(id, v)} label={`${name} on`} className="[&_label]:sr-only" />
      <span className="fx-nm w-18 text-2xs font-semibold tracking-widest text-(--tone) uppercase">{name}</span>
      <div className={`${group} flex-1 justify-around gap-3`}>{knobs}</div>
    </div>
  );
  return (
    <div className={`${content} gap-2.5`} style={toneStyle("fx")}>
      {slot("fx1On", "Chorus", (
        <>
          <ParamKnob id="chRate" ctx={ctx} label="Rate" size="sm" format={fmt.chorusHz} />
          <ParamKnob id="chDepth" ctx={ctx} label="Depth" size="sm" format={fmt.pct} />
          <ParamKnob id="chMix" ctx={ctx} label="Mix" size="sm" format={fmt.pct} />
        </>
      ))}
      {slot("fx2On", "Reverb", (
        <>
          <ParamKnob id="rvSize" ctx={ctx} label="Size" size="sm" format={fmt.pct} />
          <ParamKnob id="rvDamp" ctx={ctx} label="Damp" size="sm" format={fmt.pct} />
          <ParamKnob id="rvMix" ctx={ctx} label="Mix" size="sm" format={fmt.pct} />
        </>
      ))}
    </div>
  );
}

const ARP_MODES: { value: ArpMode; label: string }[] = (["Up", "Down", "UpDn", "Rand"] as const).map((m) => ({ value: m, label: m }));

export function ArpTab({ ctx, step }: { ctx: SynthKnobCtx; step: number }) {
  const { p, set } = ctx;
  const steps = p.arpSteps;
  const setStep = (i: number, v: number) => {
    const n = steps.slice();
    n[i] = Math.min(1, Math.max(0, v));
    set("arpSteps", n);
  };
  return (
    <div className={content}>
      <div className="flex flex-col gap-1.5">
        <Toggle checked={p.arpOn} onChange={(v) => set("arpOn", v)} label="Arp on" />
        <Segmented label="Arp mode" value={p.arpMode} onChange={(v) => set("arpMode", v)} options={ARP_MODES} />
      </div>
      <div className="flex h-16 items-end gap-[3px] rounded-control bg-well p-1.5 shadow-well">
        {steps.map((v, i) => (
          <button
            key={i}
            type="button"
            aria-label={`Step ${i + 1}: ${v > 0 ? "on" : "off"}`}
            aria-pressed={v > 0}
            onClick={() => setStep(i, v > 0 ? 0 : 0.8)}
            onWheel={(e) => setStep(i, v + (e.deltaY < 0 ? 0.1 : -0.1))}
            className={`relative h-full w-4 rounded-[2px] bg-surface-2 ${p.arpOn && step === i ? "outline outline-offset-1 outline-foreground" : ""}`}
          >
            <i
              className={`absolute inset-x-0 bottom-0 rounded-[2px] bg-(--tone) transition-[height] duration-100 ${v > 0 ? "opacity-100 shadow-[0_0_6px_color-mix(in_oklch,var(--tone)_60%,transparent)]" : "opacity-35"}`}
              style={{ height: `${Math.max(8, v * 100)}%` }}
            />
          </button>
        ))}
      </div>
      <div className={`${group} gap-3`}>
        <ParamKnob id="arpRate" ctx={ctx} label="Rate" size="sm" format={fmt.arpRate} />
        <ParamKnob id="arpGate" ctx={ctx} label="Gate" size="sm" format={fmt.pct} />
        <ParamKnob id="arpOct" ctx={ctx} label="Octaves" size="sm" format={fmt.octaves} />
        <ParamKnob id="arpSwing" ctx={ctx} label="Swing" size="sm" format={fmt.pct} />
      </div>
    </div>
  );
}
