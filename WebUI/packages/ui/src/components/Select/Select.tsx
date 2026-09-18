import {
  Select as SelectRoot,
  SelectContent,
  SelectGroup,
  SelectItem,
  SelectTrigger,
  SelectValue,
} from "@/components/ui/select";
import { cn } from "@/lib/utils";

export type SelectOption = { value: string; label: string };

export type SelectProps = {
  value: string | null;
  onChange: (value: string) => void;
  options: SelectOption[];
  /** Nome accessibile del trigger. */
  label?: string;
  placeholder?: string;
  disabled?: boolean;
  className?: string;
  id?: string;
};

export function Select({ value, onChange, options, label, placeholder = "Select…", disabled = false, className, id }: SelectProps) {
  return (
    <SelectRoot
      items={options}
      value={value}
      onValueChange={(next) => {
        if (typeof next === "string") onChange(next);
      }}
      disabled={disabled}
    >
      <SelectTrigger
        id={id}
        size="sm"
        aria-label={label}
        className={cn(
          // Slot del preset: incassato nella piastra, con la freccia su una piastrina a destra.
          "w-full border-0 bg-well! py-0 pr-1 pl-2 font-mono text-(length:--text-label)/4 shadow-well data-[size=sm]:h-7 data-[size=sm]:rounded-control!",
          "[&>svg]:size-5 [&>svg]:rounded-[2px] [&>svg]:bg-surface-3 [&>svg]:p-1 [&>svg]:text-foreground",
          className,
        )}
      >
        <SelectValue placeholder={placeholder} />
      </SelectTrigger>
      <SelectContent>
        <SelectGroup>
          {options.map((o) => (
            <SelectItem key={o.value} value={o.value} className="font-mono text-xs">
              {o.label}
            </SelectItem>
          ))}
        </SelectGroup>
      </SelectContent>
    </SelectRoot>
  );
}
