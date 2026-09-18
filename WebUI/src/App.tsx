import { SynthWindow, type SynthVariant } from "./synth/ui/SynthWindow";
import type { TabId } from "./synth/useSynth";

const VARIANTS: SynthVariant[] = ["deep", "soft", "glow"];
const TABS: TabId[] = ["env", "lfo", "mod", "fx", "arp"];

/** `?variant=deep|soft|glow&tab=env|lfo|mod|fx|arp` per provare le varianti dal browser. */
function fromQuery<T extends string>(key: string, allowed: readonly T[], fallback: T): T {
  const v = new URLSearchParams(window.location.search).get(key);
  return (allowed as readonly string[]).includes(v ?? "") ? (v as T) : fallback;
}

export default function App() {
  return <SynthWindow variant={fromQuery("variant", VARIANTS, "deep")} initialTab={fromQuery("tab", TABS, "env")} />;
}
