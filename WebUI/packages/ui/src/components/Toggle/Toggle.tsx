import { useId } from "react";
import { Switch } from "@/components/ui/switch";
import { cn } from "@/lib/utils";
import { toneStyle, type Tone } from "@/lib/tone";

export type ToggleProps = {
  checked: boolean;
  onChange: (checked: boolean) => void;
  label?: string;
  tone?: Tone;
  disabled?: boolean;
  className?: string;
  id?: string;
};

export function Toggle({ checked, onChange, label, tone, disabled = false, className, id }: ToggleProps) {
  const autoId = useId();
  const switchId = id ?? autoId;
  return (
    <div
      data-testid="toggle"
      data-slot="toggle"
      className={cn("inline-flex items-center gap-2", className)}
      style={toneStyle(tone)}
    >
      <Switch
        id={switchId}
        size="sm"
        checked={checked}
        onCheckedChange={(next) => onChange(next)}
        disabled={disabled}
        aria-label={label}
        className={cn(
          // Interruttore quadrato incassato: il pill iOS non appartiene a un pannello.
          "rounded-control! border-0 bg-well! px-0.5 shadow-well data-[size=sm]:h-4 data-[size=sm]:w-7",
          "focus-visible:ring-2 focus-visible:ring-(--tone) focus-visible:ring-offset-2 focus-visible:ring-offset-background",
          "[&_[data-slot=switch-thumb]]:size-3 [&_[data-slot=switch-thumb]]:rounded-[2px]",
          "[&_[data-slot=switch-thumb]]:bg-linear-to-b [&_[data-slot=switch-thumb]]:from-cap-hi [&_[data-slot=switch-thumb]]:to-cap-lo",
          "[&_[data-slot=switch-thumb]]:shadow-cap",
          // Il thumb ha massa: è l'unico overshoot autorizzato dell'interfaccia.
          "[&_[data-slot=switch-thumb]]:transition-transform [&_[data-slot=switch-thumb]]:duration-(--dur-state) [&_[data-slot=switch-thumb]]:ease-settle",
          "[&_[data-slot=switch-thumb][data-checked]]:translate-x-3!",
        )}
      />
      <span
        aria-hidden
        data-testid="toggle-led"
        className={cn(
          "size-1.5 shrink-0 rounded-full transition-[background-color,box-shadow] ease-snap",
          // Un LED vero si accende subito e si spegne con persistenza: l'opposto del vetro.
          checked ? "bg-(--tone) shadow-[0_0_4px_var(--tone)] duration-(--dur-press)" : "bg-led-off duration-(--dur-state)",
          disabled && "opacity-50",
        )}
      />
      {label && (
        <label
          htmlFor={switchId}
          className={cn(
            "cursor-pointer text-(length:--text-label)/4 select-none",
            checked ? "text-foreground" : "text-muted-foreground",
            disabled && "opacity-50",
          )}
        >
          {label}
        </label>
      )}
    </div>
  );
}
