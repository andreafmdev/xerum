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
  const raised = variant === "default" || variant === "secondary";
  return (
    <BaseButton
      variant={variant}
      style={{ ...toneStyle(tone), ...style }}
      className={cn(
        "rounded-control!",
        // L'anello di focus entra da appena dentro il bordo, sulla durata di stato.
        "focus-visible:ring-offset-0 motion-safe:focus-visible:scale-[1.004] motion-safe:focus-visible:duration-(--dur-state)",
        // Tasto rialzato: bordo scuro alla base, ombra di contatto, affonda alla pressione.
        raised && "border border-edge-dark shadow-cap active:shadow-none",
        variant === "default" && !toned && "bg-cap-lo bg-linear-to-b from-cap-hi to-cap-lo text-foreground hover:from-cap-rim",
        variant === "secondary" && "bg-surface-0 bg-linear-to-b from-surface-3 to-surface-0 text-foreground hover:from-cap-hi",
        toned &&
          "relative overflow-hidden bg-(--tone) text-background hover:bg-(--tone)/80 before:absolute before:inset-0 before:bg-linear-to-b before:from-cap-sheen before:to-transparent",
        variant === "outline" && "border-edge-light bg-surface-2! text-foreground hover:bg-surface-3!",
        className,
      )}
      {...props}
    />
  );
}

export { buttonVariants };
