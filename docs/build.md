# Build — SerumStyleSynth

## Prerequisites (macOS)

- Xcode (Command Line Tools / full IDE)
- CMake ≥ 3.22 (`brew install cmake`)
- Node.js 18+ (for `WebUI`)
- Git submodule for JUCE:

```bash
git submodule update --init --recursive
```

JUCE is pinned under `external/JUCE` (tag `9.0.2`).

## One command

```bash
scripts/dev.sh          # @xerum/ui build → Vite on 5173 → CMake Debug → opens the Standalone
scripts/dev.sh --web    # UI only, in the browser
PORT=5175 scripts/dev.sh
```
Ctrl-C stops the Standalone and the dev server together.

## Configure & build

`Resources/wavetables/*.xwt` must exist before configuring: the `WavetableAssets` target embeds them, and CMake stops with `FATAL_ERROR` and a pointer to `node scripts/fetch-wavetables.mjs` if the glob is empty (see `docs/architecture.md` → Wavetables). The files are committed, so this only matters if they're missing or deleted locally.

```bash
cmake --preset macos-debug
cmake --build --preset macos-debug
```

Release:

```bash
cmake --preset macos-release
cmake --build --preset macos-release
```

Artifacts land under `build/macos-debug/` (Xcode layout). With `COPY_PLUGIN_AFTER_BUILD`, AU/VST3 are also copied into the user plugin folders.

Adding a new `.cpp` file to `Source/` needs a `cmake --preset ...` reconfigure to pick it up.

Standalone app is useful for MIDI smoke tests without a DAW.

Vite serves `http://localhost:5173`. Debug builds of the editor navigate there automatically. If the server is down, the WebView shows its own load error: start Vite and reopen the editor. Release builds have no dev server — they embed the bundle and serve it through the resource provider. See "Web UI" below for the install/dev commands.

## Unit tests

`XerumTests` (`juce_add_console_app`, target defined in `CMakeLists.txt`) runs `juce::UnitTestRunner` over `Source/dsp` and `Source/engine` (`Tests/WavetableTests.cpp`, `Tests/EnvelopeFilterTests.cpp`, `Tests/EngineTests.cpp`). It links only `juce_dsp`/`juce_audio_basics` — no `juce_gui_extra`, no WebView — and does not compile `Source/bridge/*`, so `StateChannel::applyPreset` has no direct C++ test.

Build and run:

```bash
cmake --build --preset macos-debug --target XerumTests
build/macos-debug/XerumTests_artefacts/Debug/XerumTests
```

It prints one line per test and ends with `ALL TESTS PASSED` (or `TEST FAILURES`, with a non-zero exit code). Must pass before any commit that touches `Source/dsp` or `Source/engine`.

## Smoke checklist

1. Configure CMake without errors
2. Build AU + VST3 + Standalone
3. Open Standalone or load in a DAW as an **instrument**
4. Send MIDI — a note is audible, no crash
5. Automate **Master Gain** from the host
6. With `pnpm dev` (see "Web UI" below), open the editor and confirm the whole synth window renders inside it — panel, tabs and the bottom strip (wheels, performance bar, on-screen keys) are all WebView content now; there is no separate native keyboard underneath to check
7. Pick a preset (header arrows or the preset overlay) and confirm the sound changes
8. Automate `cutoff` from the host through a sweep — audible, no zipper/stepping
9. Save the session with a non-default wavetable selected, reload it, and confirm the same table is still the one playing

## Web UI

Requires pnpm 11 (`corepack enable`).

Also requires the JUCE submodule to be initialised: `WebUI` links the WebView interop
package straight out of `external/JUCE`, so `pnpm install` fails without it. The C++ build
already required the submodule; the web build now does too.

    cd WebUI
    pnpm install
    pnpm ui:build        # @xerum/ui -> packages/ui/dist
    pnpm dev             # app shell on http://localhost:5173
    pnpm ui:storybook    # component catalogue on http://localhost:6006
    pnpm ui:test         # Vitest (@xerum/ui)
    pnpm test            # Vitest (app shell: synth logic + SynthWindow smoke)

### Release (embedded WebUI)

The `macos-release` preset sets `XERUM_EMBED_WEBUI=ON`, which `juce_add_binary_data`-embeds `WebUI/dist` into the plugin binary; configuring with that option on fails with `FATAL_ERROR` if `WebUI/dist/index.html` is missing. Build the UI before configuring/building Release (see "Configure & build" above):

    cd WebUI && pnpm ui:build && pnpm build

(`pnpm ui:build` builds `@xerum/ui`; `pnpm build` runs `tsc --noEmit && vite build` into `WebUI/dist`.) Or build it as part of the CMake graph, with the `webui` custom target — from the `macos-debug` build tree, since the Release configure is what just failed:

    cmake --build --preset macos-debug --target webui

### Bridge checklist

Manual checks in a DAW after touching `Source/bridge/` or the WebUI parameter/state code:

1. Host automation of `cutoff` moves the knob in the editor.
2. Moving a knob in the editor writes host automation.
3. Save the project, reload it — the mod matrix and arp steps come back as they were.
4. Release Standalone opens with the embedded UI, without Vite running.
5. `processBlock` CPU is the same with the editor open and closed (`MeterChannel` only runs while the editor/WebView is alive).
6. Double-click a knob and confirm it returns to its default, not to zero.
