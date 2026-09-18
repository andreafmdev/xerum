import { Tabs as TabsRoot, TabsList, TabsTrigger } from "@/components/ui/tabs";
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
        className={cn("h-auto! p-0", bar ? "w-full items-stretch gap-0 rounded-none bg-surface-0 shadow-[inset_0_-1px_0_var(--color-edge-dark)]" : "gap-1")}
      >
        {items.map((item) => (
          <TabsTrigger
            key={item.value}
            value={item.value}
            style={toneStyle(item.tone)}
            className={cn(
              "flex-none text-(length:--text-label)/4 font-medium text-muted-foreground after:hidden hover:text-foreground",
              bar
                ? // Scritta di sezione, riga luminosa sotto quella attiva.
                  "relative h-7 rounded-none! border-0 px-4 text-2xs font-semibold tracking-widest uppercase data-active:bg-linear-to-b data-active:from-surface-2 data-active:to-surface-1 data-active:text-(--tone) data-active:[text-shadow:var(--tglow,none)] data-active:after:absolute data-active:after:inset-x-3 data-active:after:bottom-0 data-active:after:block data-active:after:h-0.5 data-active:after:bg-(--tone) data-active:after:opacity-100 data-active:after:shadow-[0_0_6px_var(--tone)]"
                : // Due piastrine: quella attiva è premuta dentro la piastra, non sottolineata.
                  "h-6 rounded-control! border border-transparent px-2 data-active:border-edge-dark data-active:bg-well! data-active:text-(--tone) data-active:shadow-well",
            )}
          >
            {item.label}
          </TabsTrigger>
        ))}
      </TabsList>
    </TabsRoot>
  );
}
