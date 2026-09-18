# Architecture — SerumStyleSynth

## Principle

**JUCE lives at the boundary.** The real-time synth core (`engine/`, `dsp/`) stays free of UI and host glue so wavetable / mod-matrix work can grow without rewriting the plugin adapter.

## Layers

| Layer | Path | Responsibility |
|-------|------|----------------|
| Plugin adapter | `Source/plugin/` | `AudioProcessor`, APVTS, WebView editor |
| Parameters | `Source/parameters/` | Single source of parameter IDs + layout |
| Engine | `Source/engine/` | MIDI dispatch, voice pool, block render |
| DSP stubs | `Source/dsp/` | Oscillator / filter / envelope / future `WavetableStore` |
| Web UI | `WebUI/` | App shell React + Vite (dev server → WebView); consuma `@xerum/ui` |
| UI library | `WebUI/packages/ui/` | `@xerum/ui`: componenti synth (Tailwind v4, shadcn base-nova), Storybook, test |

```
DAW MIDI ──► PluginProcessor ──► SynthEngine ──► VoiceManager ──► SynthVoice
                    │                                      │
                 APVTS                              dsp stubs (silence)
                    │
             PluginEditor (WebView) ◄── React (localhost:5173 / fallback HTML)
```

## Real-time rules

On the audio thread (`processBlock` → `SynthEngine::process`):

- No heap allocations
- No mutexes / locks
- No file or network I/O
- No logging

Cross-thread assets (wavetables, presets) must use lock-free handoff — double buffer, `AbstractFifo`, or atomic pointer swap (`WavetableStore`, phase 2).

## Phase 1 behaviour

- Instrument plugin: AU + VST3 + Standalone
- MIDI note on/off allocates voices (16-voice pool, round-robin steal)
- Output is **silence** (`util::kEnableTestTone = false`)
- Parameters: `master_gain`, `osc1_level` (latter unused until oscillator mix)

## Roadmap

1. **Scaffolding** (this phase) — build, MIDI path, WebView shell
2. **WavetableStore** — load / swap tables safely
3. **WavetableOscillator** — interpolate + advance phase
4. **WebRelay** — APVTS ↔ React
5. **ModulationMatrix** — LFO / env / macro → targets
6. **Warp / unison**
7. **FX rack**
