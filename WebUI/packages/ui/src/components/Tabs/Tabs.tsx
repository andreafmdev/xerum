import { useLayoutEffect, useRef, useState } from "react";
import { Tabs as TabsRoot, TabsList, TabsTrigger } from "@/components/ui/tabs";
import { cn } from "@/lib/utils";
import { toneStyle, type Tone } from "@/lib/tone";
import { baseWidthOf, indicatorTransform, measureIndicator, type IndicatorBox } from "@/lib/indicator";

export type TabItem = { value: string; label: string; /** Colore del singolo tab (sezione diversa per ogni pagina). */ tone?: Tone };

export type TabsProps = {
  value: string;
  onChange: (value: string) => void;
  items: TabItem[];
  tone?: Tone;
  /**
   * `plate` (default): piastrine, l'attiva è premuta nell'incasso.
   * `bar`: barra a tutta larghezza, l'attiva è illuminata con una riga sotto.
   */
  variant?: "plate" | "bar";
  className?: string;
};

/** Solo la barra dei tab: il contenuto attivo lo renderizza il consumer in base a `value`. */
export function Tabs({ value, onChange, items, tone, variant = "plate", className }: TabsProps) {
  const bar = variant === "bar";
  const list = useRef<HTMLDivElement>(null);
  const triggers = useRef<(HTMLElement | null)[]>([]);
  const indicator = useRef<HTMLSpanElement>(null);
  const [box, setBox] = useState<IndicatorBox>({ x: 0, width: 0 });
  // Larghezza renderizzata dell'indicatore stesso, base vera dello scaleX: mai assunta, sempre
  // letta con offsetWidth (vedi lib/indicator.ts e Segmented, stesso identico meccanismo).
  const [baseWidth, setBaseWidth] = useState(0);
  const active = items.findIndex((item) => item.value === value);

  // Base UI's Tabs.Indicator pubblica la geometria solo come lunghezze CSS (--active-tab-left,
  // --active-tab-width): utili per un `width`/`left` diretto, ma il CSS puro non può trasformare
  // una lunghezza in un fattore di scala adimensionale, e senza scaleX la larghezza può solo
  // scattare all'istante da un valore all'altro, mai scorrere. Per farla scorrere insieme alla
  // posizione serve misurare in JS e animare transform (translateX + scaleX): esattamente il
  // meccanismo già scritto, testato e debuggato due volte per Segmented. Qui si misura il tab
  // attivo direttamente (niente Tabs.Indicator di Base UI come elemento ospite: aggiungerlo
  // avrebbe introdotto un secondo sistema di misura — il suo ResizeObserver interno — in corsa
  // con questo, lo stesso genere di problema di ordinamento che ha causato i bug di Segmented).
  useLayoutEffect(() => {
    if (!bar) return;
    const container = list.current;
    const item = triggers.current[active];
    if (!container || !item) return;
    const measure = () => {
      setBox(measureIndicator(container, item));
      setBaseWidth(indicator.current ? baseWidthOf(indicator.current) : 0);
    };
    measure();
    const ro = new ResizeObserver(measure);
    ro.observe(container);
    ro.observe(item);
    return () => ro.disconnect();
  }, [bar, active, items.length]);

  return (
    <TabsRoot
      data-testid="tabs"
      data-variant={variant}
      value={value}
      onValueChange={(next) => onChange(String(next))}
      className={className}
      style={toneStyle(tone)}
    >
      <TabsList
        ref={list}
        variant="line"
        activateOnFocus
        className={cn("h-auto! p-0", bar ? "relative w-full items-stretch gap-0 rounded-none bg-surface-0 shadow-[inset_0_-1px_0_var(--color-edge-dark)]" : "gap-1")}
      >
        {items.map((item, i) => (
          <TabsTrigger
            key={item.value}
            ref={(el) => {
              triggers.current[i] = el;
            }}
            value={item.value}
            style={toneStyle(item.tone)}
            className={cn(
              "flex-none text-(length:--text-label)/4 font-medium text-muted-foreground after:hidden hover:text-foreground",
              bar
                ? // Scritta di sezione; la riga luminosa sotto l'attiva la disegna ora l'indicatore che scorre.
                  "relative h-7 rounded-none! border-0 px-4 text-2xs font-semibold tracking-widest uppercase data-active:bg-linear-to-b data-active:from-surface-2 data-active:to-surface-1 data-active:text-(--tone) data-active:[text-shadow:var(--tglow,none)]"
                : // Due piastrine: quella attiva è premuta dentro la piastra, non sottolineata.
                  "h-6 rounded-control! border border-transparent px-2 data-active:border-edge-dark data-active:bg-well! data-active:text-(--tone) data-active:shadow-well",
            )}
          >
            {item.label}
          </TabsTrigger>
        ))}
        {bar && (
          <span
            ref={indicator}
            aria-hidden
            data-testid="tabs-indicator"
            data-measured={box.width > 0}
            // Stesso meccanismo di Segmented (lib/indicator.ts): origin-left + `width: 1` come
            // base reale dello scaleX, transform (translateX + scaleX) calcolato in JS dalla
            // geometria misurata. Mai `width`, che il sistema di motion vieta di animare.
            // Niente box-shadow qui: un glow radiale simmetrico (0 0 Npx, nessun offset) si
            // stirerebbe in modo asimmetrico sotto scaleX (a differenza dell'ombra "a cappuccio"
            // di Segmented, che è verticale/direzionale e ne risente pochissimo) — per un
            // componente di libreria che non conosce le larghezze reali dei tab del consumer,
            // il colore di sfondo da solo resta leggibile senza rischiare quella distorsione.
            className="pointer-events-none absolute bottom-0 left-0 h-0.5 origin-left bg-(--tone) transition-transform duration-(--dur-state) ease-glass data-[measured=false]:opacity-0"
            style={{ width: 1, transform: indicatorTransform(box, baseWidth) }}
          />
        )}
      </TabsList>
    </TabsRoot>
  );
}
