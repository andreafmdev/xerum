import { useMemo, useState, type ReactNode } from "react";
import { Segmented, Tabs, Toggle, toneStyle } from "@xerum/ui";
import { X } from "lucide-react";
import { useBoolParam, useChoiceParam, useFloatParam } from "../../juce/hooks";
import { envPath, lfoPath } from "../curves";
import { formatValue, paramLabel, signedInt } from "../mapping";
import { MOD_SOURCES, SOURCE_LABEL, SOURCE_TONE, type LfoShape } from "../mod";
import { PARAM_SPECS, type ParamId } from "../params.generated";
import { useMeterFrame } from "./MetersContext";
import { ModChip } from "./ModChip";
import { ParamKnob } from "./ParamKnob";
import { useDirty, useSynthCtx } from "./SynthContext";
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

/**
 * I due inviluppi, con gli stessi quattro knob.
 *
 * La scelta: un selettore ENV / ENV2 che ricabla i knob gia' presenti, invece di otto knob
 * affiancati. Il plate del tab e' alto 124 px e largo quanto lo chassis — i quattro knob, lo
 * schermo dell'inviluppo e i due knob piccoli lo riempiono gia'; raddoppiarli avrebbe voluto
 * dire rimpicciolirli tutti, e il grafico dell'inviluppo (che e' il modo in cui si legge un
 * ADSR) sarebbe rimasto uno solo per due forme diverse. Cosi' invece lo schermo mostra sempre
 * l'inviluppo che si sta modificando.
 *
 * `envVel` e `envCurve` restano visibili ma disabilitati su ENV2: appartengono all'inviluppo
 * d'ampiezza e il secondo non ne ha di propri (la velocity e' gia' una sorgente del matrix).
 * Disabilitati e non nascosti perche' sparire farebbe saltare la riga di knob a ogni scambio.
 */
const ENV_SELECTOR = [
  { value: "env", label: "ENV" },
  { value: "env2", label: "ENV2" },
] as const;

/** I quattro slot dell'inviluppo scelto, nell'ordine attacco/decay/sustain/release. */
const ENV_IDS = {
  env: ["att", "dec", "sus", "rel"],
  env2: ["att2", "dec2", "sus2", "rel2"],
} as const satisfies Record<"env" | "env2", readonly [ParamId, ParamId, ParamId, ParamId]>;

export function EnvTab() {
  const [which, setWhich] = useState<"env" | "env2">("env");
  // Tutti e otto gli hook, sempre: l'ordine delle chiamate non puo' dipendere dalla scelta.
  const values = {
    att: useFloatParam("att").value,
    dec: useFloatParam("dec").value,
    sus: useFloatParam("sus").value,
    rel: useFloatParam("rel").value,
    att2: useFloatParam("att2").value,
    dec2: useFloatParam("dec2").value,
    sus2: useFloatParam("sus2").value,
    rel2: useFloatParam("rel2").value,
  };
  const [aId, dId, sId, rId] = ENV_IDS[which];
  const [a, dv, sv, r] = [values[aId], values[dId], values[sId], values[rId]];
  const isSecond = which === "env2";
  const W = 200;
  const H = 66;
  const d = useMemo(() => envPath(a, dv, sv, r, W, H), [a, dv, sv, r]);
  return (
    <div className={content}>
      <div className={screen} style={{ width: W, height: H }}>
        <svg width={W} height={H} style={toneStyle(isSecond ? SOURCE_TONE.env2 : SOURCE_TONE.env)}>
          <path d={`${d} L${W - 6} ${H - 6} L6 ${H - 6}Z`} className="fill-(--tone)" opacity={0.12} />
          <path d={d} className="fill-none stroke-(--tone) [filter:var(--glow)]" strokeWidth={1.8} />
        </svg>
      </div>
      <div className={`${group} gap-3.5`}>
        <ParamKnob key={aId} id={aId} />
        <ParamKnob key={dId} id={dId} />
        <ParamKnob key={sId} id={sId} />
        <ParamKnob key={rId} id={rId} />
      </div>
      <div className={vsep} />
      <div className={group}>
        <ParamKnob id="envVel" size="sm" disabled={isSecond} />
        <ParamKnob id="envCurve" size="sm" disabled={isSecond} />
      </div>
      <div className={vsep} />
      <div className="flex flex-col gap-1.5">
        <Segmented label="Inviluppo da modificare" value={which} onChange={setWhich} options={[...ENV_SELECTOR]} />
        {/* Il testo resta su due righe: sotto il selettore ci sono ~56 px prima che il plate
            (h-31, overflow-hidden) cominci a tagliare. */}
        <p className="max-w-38 text-[11px] leading-snug text-text-dim">
          Trascina <b className="text-env">ENV</b> o <b className="text-fx">ENV2</b> su un knob.
        </p>
      </div>
    </div>
  );
}

/** Divisioni di nota mostrate al posto degli hertz quando l'LFO è in sync. */
const DIVISIONS = ["1/16", "1/8", "1/4", "1/2", "1", "2"];

export function LfoTab() {
  const lshape = useChoiceParam("lshape");
  const lsync = useBoolParam("lsync");
  const lretrig = useBoolParam("lretrig");
  const dirty = useDirty();
  const W = 200;
  const H = 66;
  const d = useMemo(() => lfoPath(lshape.value as LfoShape, W, H), [lshape.value]);
  return (
    <div className={content}>
      <div className={screen} style={{ width: W, height: H }}>
        <svg width={W} height={H}>
          <line x1="6" x2={W - 6} y1={H / 2} y2={H / 2} className="stroke-line-strong" opacity={0.4} />
          <path d={d} className="fill-none stroke-(--tone) [filter:var(--glow)]" strokeWidth={1.6} />
          <LfoDot w={W} h={H} />
        </svg>
      </div>
      <div className="flex flex-col gap-2">
        <Segmented label="LFO shape" value={lshape.value} onChange={dirty(lshape.set)} options={lshape.options} />
        <div className="flex items-center gap-2.5">
          <Toggle checked={lsync.checked} onChange={dirty(lsync.set)} label="Sync" />
          <Toggle checked={lretrig.checked} onChange={dirty(lretrig.set)} label="Retrig" />
        </div>
      </div>
      <div className={`${group} gap-3.5`}>
        <ParamKnob
          id="lrate"
          format={(v) => (lsync.checked ? DIVISIONS[Math.min(5, Math.floor(v * 6))]! : formatValue(PARAM_SPECS.lrate, v))}
        />
        <ParamKnob id="lphase" />
        <ParamKnob id="lfade" />
      </div>
    </div>
  );
}

/** Solo il puntino segue i meter: il resto del tab non si ridisegna a 30 Hz. */
function LfoDot({ w, h }: { w: number; h: number }) {
  // Il bridge manda il livello dell'LFO, non la sua fase: il puntino sta al centro
  // dello schermo e sale/scende con il valore.
  const { lfo } = useMeterFrame();
  return <circle cx={w / 2} cy={h / 2 - lfo * (h / 2 - 8)} r="3.5" className="fill-foreground" />;
}

export function ModTab() {
  const { mods, setDepth, removeMod } = useSynthCtx();
  if (!mods.length) {
    return (
      <div className={content}>
        <p className="text-[11px] leading-snug text-text-dim">
          Nessuna assegnazione. Trascina <b className="text-lfo">LFO</b>, <b className="text-env">ENV</b>,{" "}
          <b className="text-fx">ENV2</b>, <b className="text-master">VEL</b> o <b className="text-filter">MW</b> dalla
          barra dei tab su un qualsiasi knob.
        </p>
      </div>
    );
  }
  return (
    <div className="grid flex-1 grid-cols-2 content-start gap-x-6 px-3.5 py-1.5">
      {mods.map((m, i) => {
        const target = paramLabel(PARAM_SPECS[m.target]);
        const name = `${SOURCE_LABEL[m.src]} → ${target}`;
        return (
          <div key={`${m.src}-${m.target}`} className="flex h-5.5 items-center gap-2.5 text-[10px]" style={toneStyle(SOURCE_TONE[m.src])}>
            <span className="w-8 font-semibold tracking-wider text-(--tone)">{SOURCE_LABEL[m.src]}</span>
            <span className="text-text-dim">→</span>
            <span className="w-22 truncate text-muted-foreground">{target}</span>
            <input type="range" min="-1" max="1" step="0.01" value={m.depth} aria-label={`Depth ${name}`} onChange={(e) => setDepth(i, +e.target.value)} className="sx-range" />
            <span className="w-8 text-right font-mono text-foreground tabular-nums">{signedInt(Math.round(m.depth * 100))}</span>
            <button type="button" aria-label={`Remove ${name}`} onClick={() => removeMod(i)} className="text-text-dim hover:text-destructive">
              <X className="size-3" />
            </button>
          </div>
        );
      })}
    </div>
  );
}

export function FxTab() {
  const fx1On = useBoolParam("fx1On");
  const fx2On = useBoolParam("fx2On");
  const dirty = useDirty();
  const slot = (on: boolean, setOn: (v: boolean) => void, name: string, knobs: ReactNode) => (
    <div className={`flex flex-1 items-center gap-2.5 rounded-control bg-surface-1 px-2.5 py-1 shadow-[inset_0_0_0_1px_var(--color-edge-dark),inset_0_1px_0_var(--color-edge-light)] ${on ? "" : "[&_.fx-nm]:opacity-45 [&_[data-slot=knob]]:opacity-45"}`}>
      <Toggle checked={on} onChange={dirty(setOn)} label={`${name} on`} className="[&_label]:sr-only" />
      <span className="fx-nm w-18 text-2xs font-semibold tracking-widest text-(--tone) uppercase">{name}</span>
      <div className={`${group} flex-1 justify-around gap-3`}>{knobs}</div>
    </div>
  );
  return (
    <div className={`${content} gap-2.5`} style={toneStyle("fx")}>
      {slot(fx1On.checked, fx1On.set, "Chorus", (
        <>
          <ParamKnob id="chRate" size="sm" />
          <ParamKnob id="chDepth" size="sm" />
          <ParamKnob id="chMix" size="sm" />
        </>
      ))}
      {slot(fx2On.checked, fx2On.set, "Reverb", (
        <>
          <ParamKnob id="rvSize" size="sm" />
          <ParamKnob id="rvDamp" size="sm" />
          <ParamKnob id="rvMix" size="sm" />
        </>
      ))}
    </div>
  );
}

/** Riquadro dello step in riproduzione: l'unico pezzo dell'arp abbonato ai meter. */
function ArpPlayhead({ index, on }: { index: number; on: boolean }) {
  const { arpStep } = useMeterFrame();
  if (!on || arpStep !== index) return null;
  return <i aria-hidden className="absolute inset-0 rounded-[2px] outline outline-offset-1 outline-foreground" />;
}

export function ArpTab() {
  const arpOn = useBoolParam("arpOn");
  const arpMode = useChoiceParam("arpMode");
  const { arpSteps, setArpSteps } = useSynthCtx();
  const dirty = useDirty();
  const setStep = (i: number, v: number) => {
    const n = arpSteps.slice();
    n[i] = Math.min(1, Math.max(0, v));
    setArpSteps(n);
  };
  return (
    <div className={content}>
      <div className="flex flex-col gap-1.5">
        <Toggle checked={arpOn.checked} onChange={dirty(arpOn.set)} label="Arp on" />
        <Segmented label="Arp mode" value={arpMode.value} onChange={dirty(arpMode.set)} options={arpMode.options} />
      </div>
      <div className="flex h-16 items-end gap-[3px] rounded-control bg-well p-1.5 shadow-well">
        {arpSteps.map((v, i) => (
          <button
            key={i}
            type="button"
            aria-label={`Step ${i + 1}: ${v > 0 ? "on" : "off"}`}
            aria-pressed={v > 0}
            onClick={() => setStep(i, v > 0 ? 0 : 0.8)}
            onWheel={(e) => setStep(i, v + (e.deltaY < 0 ? 0.1 : -0.1))}
            className="relative h-full w-4 rounded-[2px] bg-surface-2"
          >
            <ArpPlayhead index={i} on={arpOn.checked} />
            <i
              className={`absolute inset-x-0 bottom-0 rounded-[2px] bg-(--tone) transition-[height] duration-100 ${v > 0 ? "opacity-100 shadow-[0_0_6px_color-mix(in_oklch,var(--tone)_60%,transparent)]" : "opacity-35"}`}
              style={{ height: `${Math.max(8, v * 100)}%` }}
            />
          </button>
        ))}
      </div>
      <div className={`${group} gap-3`}>
        <ParamKnob id="arpRate" size="sm" />
        <ParamKnob id="arpGate" size="sm" />
        <ParamKnob id="arpOct" size="sm" />
        <ParamKnob id="arpSwing" size="sm" />
      </div>
    </div>
  );
}
