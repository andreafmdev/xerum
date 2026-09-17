import type { ComponentProps } from "react";
import { Button as BaseButton, buttonVariants } from "@/components/ui/button";
import { cn } from "@/lib/utils";
import { toneStyle, type Tone } from "@/lib/tone";

export type ButtonProps = ComponentProps<typeof BaseButton> & {
  /** Colore di sezione: la variante `default` usa `--tone` come sfondo. */
  tone?: Tone;
};

export function Button({ tone, variant = "default", className, style, ...props }: ButtonProps) {
  const toned = tone !== undefined && variant === "default";
  return (
    <BaseButton
      variant={variant}
      style={{ ...toneStyle(tone), ...style }}
      className={cn(toned && "bg-(--tone) text-background hover:bg-(--tone)/80", className)}
      {...props}
    />
  );
}

export { buttonVariants };
