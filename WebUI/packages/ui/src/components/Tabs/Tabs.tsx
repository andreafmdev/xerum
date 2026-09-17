import { Tabs as TabsRoot, TabsList, TabsTrigger } from "@/components/ui/tabs";
import { cn } from "@/lib/utils";
import { toneStyle, type Tone } from "@/lib/tone";

export type TabItem = { value: string; label: string };

export type TabsProps = {
  value: string;
  onChange: (value: string) => void;
  items: TabItem[];
  tone?: Tone;
  className?: string;
};

/** Solo la barra dei tab: il contenuto attivo lo renderizza il consumer in base a `value`. */
export function Tabs({ value, onChange, items, tone, className }: TabsProps) {
  return (
    <TabsRoot
      data-testid="tabs"
      value={value}
      onValueChange={(next) => onChange(String(next))}
      className={className}
      style={toneStyle(tone)}
    >
      <TabsList variant="line" activateOnFocus className="h-7 gap-3 p-0">
        {items.map((item) => (
          <TabsTrigger
            key={item.value}
            value={item.value}
            className={cn(
              "px-1 text-2xs font-medium tracking-wider uppercase",
              "data-active:text-(--tone) after:bg-(--tone)",
            )}
          >
            {item.label}
          </TabsTrigger>
        ))}
      </TabsList>
    </TabsRoot>
  );
}
