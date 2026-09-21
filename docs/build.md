# Build — Xerum

How the two halves of the editor are served (dev server vs embedded bundle) is explained in
**[`docs/bridge.md`](bridge.md)** → *Serving the page*.

## Prerequisites (macOS)

- Xcode (Command Line Tools / full IDE), recent enough for C++23 — Apple clang 16 or newer
- CMake ≥ 3.22 (`brew install cmake`)
- Node.js 18+ (for `WebUI`)
- Git submodule for JUCE:

```bash
git submodule update --init --recursive
```

JUCE is pinned under `external/JUCE` (tag `9.0.2`).

The project builds as **C++23** (`CMAKE_CXX_STANDARD 23`). JUCE 9 declares `cxx_std_17` as an
INTERFACE feature — a floor for consumers, not a ceiling — so raising the standard does not
violate it. Clean builds of every target at 17, 20 and 23 produced the same passing tests and the
same ten warnings, with none added. The thing to watch is the standard library rather than the
compiler: Apple clang implements the C++23 language, but libc++ lags on parts of the library. A
missing C++23 header or function is that lag, not a misconfiguration — work around it rather than
lowering the standard.

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

Artifacts land under `build/macos-debug/Xerum_artefacts/` (Xcode layout). With `XERUM_COPY_PLUGIN` (default ON), AU/VST3 are also copied into the user plugin folders.

Adding a new `.cpp` file to `Source/` needs a `cmake --preset ...` reconfigure to pick it up.

Standalone app is useful for MIDI smoke tests without a DAW.

Vite serves `http://localhost:5173`. Debug builds of the editor navigate there automatically. If the server is down, the WebView shows its own load error: start Vite and reopen the editor. Release builds have no dev server — they embed the bundle and serve it through the resource provider. See "Web UI" below for the install/dev commands.

## Unit tests

`XerumTests` (`juce_add_console_app`, target defined in `CMakeLists.txt`) runs `juce::UnitTestRunner` over `Source/dsp`, `Source/engine` and `Source/parameters` (the suites in `Tests/`). It compiles the same `XerumCore` source list as the plugin, links `juce_dsp`/`juce_audio_basics`/`juce_audio_processors`/`juce_data_structures` — no `juce_gui_extra`, no WebView — and compiles `Source/plugin/PluginProcessor.cpp` without its editor (`XERUM_HEADLESS_TESTS`), so the saved-state round-trip is tested through the real processor (`Tests/PluginProcessorTests.cpp`). `Source/bridge/*` is still not compiled: `StateChannel::applyPreset` has no direct C++ test.

Build and run through ctest:

```bash
cmake --build --preset macos-debug --target XerumTests
ctest --preset macos-debug
```

or run the binary directly (`build/macos-debug/XerumTests_artefacts/Debug/XerumTests`): it prints one line per test and ends with `ALL TESTS PASSED` (or `TEST FAILURES`, with a non-zero exit code). Must pass before any commit that touches `Source/dsp`, `Source/engine` or `Source/parameters`.

CI (`.github/workflows/ci.yml`) runs the same suite with the `ninja-debug` preset (`brew install ninja`; `XERUM_COPY_PLUGIN=OFF`), plus the Web UI tests and build.

## Plugin identity

The plugin is `Xerum` (`PRODUCT_NAME`, bundle `com.xerum.xerum`, manufacturer code `Xeru`, plugin code `Xrm1`). It was `SerumStyleSynth` with code `Ss01` until 2026-09-20: DAW sessions saved with the old identity do not find the new instrument and must be recreated (the parameter state itself is unchanged). The old bundles under `~/Library/Audio/Plug-Ins/{Components,VST3}/SerumStyleSynth.*` are not removed by the build: delete them by hand.

`XERUM_COPY_PLUGIN=OFF` skips copying AU/VST3 into the user plugin folders (CI, test builds).

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
7. Lo Standalone si apre senza barra del titolo, ma il semaforo c'è e i tre bottoni funzionano.
8. Entra in full screen (bottone verde o ⌃⌘F) ed esci di nuovo: la striscia della barra del
   titolo non deve essere tornata. È il giro di verifica del Critical corretto in questa
   feature — `resized()` deve riapplicare `makeWindowChromeless` perché uscire dal full screen
   fa scattare `windowDidExitFullScreen`, che dentro JUCE riassegna lo `styleMask` senza
   `NSWindowStyleMaskFullSizeContentView` (vedi **Lo Standalone e la sua finestra** in
   `docs/architecture.md`); saltare questo passo lascerebbe la regressione silenziosa.
9. L'header trascina la finestra; una manopola no. Doppio clic sull'header: la finestra si ingrandisce.
10. Il ridimensionamento rispetta ancora il vincolo altezza/larghezza del constrainer.
11. Cambio di device e di buffer size mentre una nota suona: nessun crash, nessuna nota appesa.
12. Interfaccia audio staccata a caldo: il pannello si aggiorna da solo.
13. Riaperta l'app, device audio e ingresso MIDI scelti sono quelli di prima.
14. Il ripristino di fabbrica chiede conferma prima di agire.
