import { Meter } from "@xerum/ui";

type Props = { inL: number; outL: number; voices: number; cpu: string };

export function Footer({ inL, outL, voices, cpu }: Props) {
  return (
    <footer className="flex h-7 shrink-0 items-center gap-3.5 px-1.5 font-mono text-2xs tracking-wider text-text-dim">
      <span>IN</span>
      <Meter level={inL} label="Input" tone="osc" />
      <span className="flex-1" />
      <span>
        VOICES <b className="font-medium text-foreground">{voices}</b>/16
      </span>
      <span>
        CPU <b className="font-medium text-foreground">{cpu}%</b>
      </span>
      <span className="flex-1" />
      <Meter level={outL} label="Output" tone="master" />
      <span>OUT</span>
    </footer>
  );
}
