import { Meter } from "@xerum/ui";
import { useBoolParam, useChoiceParam } from "../../juce/hooks";
import { useMeterFrame } from "./MetersContext";

/** Barra bassa: meter in/out dal bridge, conteggi di voci e CPU derivati dai parametri. */
export function Footer() {
  const meters = useMeterFrame();
  const arpOn = useBoolParam("arpOn");
  const fx2On = useBoolParam("fx2On");
  const voiceMode = useChoiceParam("voiceMode");
  const unison = useChoiceParam("unison");
  const voices = arpOn.checked ? 1 : voiceMode.value === "Poly" ? 4 : 1;
  // Numeri di comodo, non misure vere: l'host non le espone ancora.
  const cpu = 3 + (fx2On.checked ? 2 : 0) + unison.options.findIndex((o) => o.value === unison.value);
  return (
    <footer className="flex h-7 shrink-0 items-center gap-3.5 px-1.5 font-mono text-2xs tracking-wider text-text-dim">
      <span>IN</span>
      <Meter level={meters.in} label="Input" tone="osc" />
      <span className="flex-1" />
      <span>
        VOICES <b className="font-medium text-foreground">{voices}</b>/16
      </span>
      <span>
        CPU <b className="font-medium text-foreground">{cpu}%</b>
      </span>
      <span className="flex-1" />
      <Meter level={meters.out} label="Output" tone="master" />
      <span>OUT</span>
    </footer>
  );
}
