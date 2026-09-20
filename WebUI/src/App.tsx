import { LazyMotion, MotionConfig, domAnimation } from "motion/react";
import { SynthWindow, type SynthVariant } from "./synth/ui/SynthWindow";
import type { TabId } from "./synth/useSynth";

const VARIANTS: SynthVariant[] = ["glass", "metal", "deep", "soft", "glow"];
const TABS: TabId[] = ["env", "lfo", "mod", "fx", "arp"];

/** `?variant=glass|metal|deep|soft|glow&tab=env|lfo|mod|fx|arp` per provare le varianti dal browser.
    `?gutter=0` (lo passa l'host JUCE) toglie il margine attorno allo chassis. */
function fromQuery<T extends string>(key: string, allowed: readonly T[], fallback: T): T {
  const v = new URLSearchParams(window.location.search).get(key);
  return (allowed as readonly string[]).includes(v ?? "") ? (v as T) : fallback;
}

function gutterFromQuery(fallback: number) {
  const raw = new URLSearchParams(window.location.search).get("gutter");
  if (raw === null) return fallback;
  const v = Number(raw);
  return Number.isFinite(v) && v >= 0 ? v : fallback;
}

export default function App() {
  return (
    // `strict` fa fallire ogni `motion.*`: obbliga `m.*` e tiene il bundle sul solo
    // `domAnimation`, che di suo non contiene le layout animations (vietate dalla spec).
    <LazyMotion features={domAnimation} strict>
      {/* `reducedMotion="user"` fa si' che OGNI `m.*` di questo albero (il crossfade dei tab in
          Tabs.tsx, l'enter/exit di PresetOverlay.tsx, e qualsiasi `m.*` una task futura
          aggiunga) rispetti `prefers-reduced-motion` da solo, senza che ogni componente debba
          ricordarsi di chiamare `useReducedMotion()`. E' l'equivalente, per la libreria, della
          regola CSS globale in index.css: un solo punto che vale per tutto l'albero, presente e
          futuro, invece di una deroga per componente. `LazyContext` (da LazyMotion, sopra) e
          `MotionConfigContext` (da MotionConfig, qui sotto) sono due context React distinti e
          indipendenti — nessuno dei due legge l'altro — quindi l'ordine di annidamento fra i due
          provider non cambia il comportamento; MotionConfig sta dentro LazyMotion solo perche'
          e' concettualmente piu' vicino all'albero che configura. */}
      <MotionConfig reducedMotion="user">
        <SynthWindow
          variant={fromQuery("variant", VARIANTS, "glass")}
          initialTab={fromQuery("tab", TABS, "env")}
          gutter={gutterFromQuery(16)}
        />
      </MotionConfig>
    </LazyMotion>
  );
}
