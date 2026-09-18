import { SynthWindow, type SynthVariant } from "./synth/ui/SynthWindow";
import type { TabId } from "./synth/useSynth";

const VARIANTS: SynthVariant[] = ["deep", "soft", "glow"];
const TABS: TabId[] = ["env", "lfo", "mod", "fx", "arp"];

/** `?variant=deep|soft|glow&tab=env|lfo|mod|fx|arp` per provare le varianti dal browser.
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
    <SynthWindow
      variant={fromQuery("variant", VARIANTS, "deep")}
      initialTab={fromQuery("tab", TABS, "env")}
      gutter={gutterFromQuery(16)}
    />
  );
}
