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
        className="data-checked:bg-(--tone) focus-visible:ring-(--tone)/50"
      />
      {label && (
        <label
          htmlFor={switchId}
          className={cn(
            "cursor-pointer text-2xs uppercase tracking-wider select-none",
            checked ? "text-(--tone)" : "text-muted-foreground",
            disabled && "opacity-50",
          )}
        >
          {label}
        </label>
      )}
    </div>
  );
}
