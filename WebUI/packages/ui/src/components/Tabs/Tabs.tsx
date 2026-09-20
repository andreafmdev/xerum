import { Tabs as TabsRoot, TabsList, TabsTrigger, TabsIndicator } from "@/components/ui/tabs";
import { cn } from "@/lib/utils";
import { toneStyle, type Tone } from "@/lib/tone";

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
        variant="line"
        activateOnFocus
        className={cn("h-auto! p-0", bar ? "relative w-full items-stretch gap-0 rounded-none bg-surface-0 shadow-[inset_0_-1px_0_var(--color-edge-dark)]" : "gap-1")}
      >
        {items.map((item) => (
          <TabsTrigger
            key={item.value}
            value={item.value}
            style={toneStyle(item.tone)}
            className={cn(
              "flex-none text-(length:--text-label)/4 font-medium text-muted-foreground after:hidden hover:text-foreground",
              bar
                ? // Scritta di sezione; la riga luminosa sotto l'attiva la disegna ora TabsIndicator.
                  "relative h-7 rounded-none! border-0 px-4 text-2xs font-semibold tracking-widest uppercase data-active:bg-linear-to-b data-active:from-surface-2 data-active:to-surface-1 data-active:text-(--tone) data-active:[text-shadow:var(--tglow,none)]"
                : // Due piastrine: quella attiva è premuta dentro la piastra, non sottolineata.
                  "h-6 rounded-control! border border-transparent px-2 data-active:border-edge-dark data-active:bg-well! data-active:text-(--tone) data-active:shadow-well",
            )}
          >
            {item.label}
          </TabsTrigger>
        ))}
        {bar && (
          <TabsIndicator
            data-testid="tabs-indicator"
            // Base UI pubblica la geometria del tab attivo come custom properties: qui diventano
            // una sola riga che scorre, senza misurare niente a mano (a differenza di Segmented,
            // che essendo un radiogroup non può usare questa parte di Tabs).
            className="absolute bottom-0 left-0 h-0.5 w-[var(--active-tab-width)] translate-x-[var(--active-tab-left)] bg-(--tone) shadow-[0_0_6px_var(--tone)] transition-[translate,scale] duration-(--dur-state) ease-glass"
          />
        )}
      </TabsList>
    </TabsRoot>
  );
}
