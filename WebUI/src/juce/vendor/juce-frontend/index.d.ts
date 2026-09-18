// Tipi per la copia vendor di juce-framework-frontend (index.js): il modulo
// originale e' JavaScript puro, qui dichiariamo solo cio' che il JuceBackend usa.

export interface ListenerList { addListener(fn: () => void): number; removeListener(id: number): void }
export interface SliderState { getNormalisedValue(): number; setNormalisedValue(v: number): void; sliderDragStarted(): void; sliderDragEnded(): void; valueChangedEvent: ListenerList; properties: { start: number; end: number; skew: number; interval: number } }
export interface ToggleState { getValue(): boolean; setValue(v: boolean): void; valueChangedEvent: ListenerList }
export interface ComboBoxState { getChoiceIndex(): number; setChoiceIndex(i: number): void; valueChangedEvent: ListenerList; properties: { choices: string[] } }
export function getSliderState(name: string): SliderState;
export function getToggleState(name: string): ToggleState;
export function getComboBoxState(name: string): ComboBoxState;
export function getNativeFunction(name: string): (...args: unknown[]) => Promise<unknown>;
