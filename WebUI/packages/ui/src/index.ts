import "./index.css";

export { cn } from "@/lib/utils";
export { TONES, toneStyle, type Tone } from "@/lib/tone";
export { useDragValue, type UseDragValueOptions, type UseDragValueResult } from "@/hooks/useDragValue";

export { Knob, modRange, MOD_DRAG_TYPE, type KnobProps, type KnobMod } from "@/components/Knob/Knob";
export { Segmented, type SegmentedOption, type SegmentedProps } from "@/components/Segmented/Segmented";
export { Stepper, type StepperProps } from "@/components/Stepper/Stepper";
export { Meter, litSegments, type MeterProps } from "@/components/Meter/Meter";
export { Fader, type FaderProps } from "@/components/Fader/Fader";
export { Button, buttonVariants, type ButtonProps } from "@/components/Button/Button";
export { Toggle, type ToggleProps } from "@/components/Toggle/Toggle";
export { Select, type SelectOption, type SelectProps } from "@/components/Select/Select";
export { Tabs, type TabItem, type TabsProps } from "@/components/Tabs/Tabs";
export { ValueReadout, type ValueReadoutProps } from "@/components/ValueReadout/ValueReadout";
export { SectionHeader, type SectionHeaderProps } from "@/components/SectionHeader/SectionHeader";
export { Panel, type PanelProps } from "@/components/Panel/Panel";
export { WavetableDisplay, frameIndex, type WavetableDisplayProps } from "@/components/WavetableDisplay/WavetableDisplay";

// Primitive shadcn riesportate as-is
export { Tooltip, TooltipContent, TooltipProvider, TooltipTrigger } from "@/components/ui/tooltip";
export { Separator } from "@/components/ui/separator";
export { Label } from "@/components/ui/label";
