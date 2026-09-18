# Build — SerumStyleSynth

## Prerequisites (macOS)

- Xcode (Command Line Tools / full IDE)
- CMake ≥ 3.22 (`brew install cmake`)
- Node.js 18+ (for `WebUI`)
- Git submodule for JUCE:

```bash
git submodule update --init --recursive
```

JUCE is pinned under `external/JUCE` (tag `8.0.6`).

## One command

```bash
scripts/dev.sh          # @xerum/ui build → Vite on 5173 → CMake Debug → opens the Standalone
scripts/dev.sh --web    # UI only, in the browser
PORT=5175 scripts/dev.sh
```
Ctrl-C stops the Standalone and the dev server together.

## Configure & build

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

Vite serves `http://localhost:5173`. Debug builds of the editor navigate there automatically. If the server is down, reopen after starting Vite (or use the embedded fallback HTML via the resource provider in Release). See "Web UI" below for the install/dev commands.

## Smoke checklist

1. Configure CMake without errors
2. Build AU + VST3 + Standalone
3. Open Standalone or load in a DAW as an **instrument**
4. Send MIDI — no crash; silence is expected
5. Automate **Master Gain** from the host
6. With `pnpm dev` (see "Web UI" below), open the editor and confirm the React placeholder

## Web UI

Requires pnpm 11 (`corepack enable`).

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

(`pnpm ui:build` builds `@xerum/ui`; `pnpm build` runs `tsc --noEmit && vite build` into `WebUI/dist`.) Or build it as part of the CMake graph, with the `webui` custom target:

    cmake --build --preset macos-release --target webui

### Bridge checklist

Manual checks in a DAW after touching `Source/bridge/` or the WebUI parameter/state code:

1. Host automation of `cutoff` moves the knob in the editor.
2. Moving a knob in the editor writes host automation.
3. Save the project, reload it — the mod matrix and arp steps come back as they were.
4. Release Standalone opens with the embedded UI, without Vite running.
5. `processBlock` CPU is the same with the editor open and closed (`MeterChannel` only runs while the editor/WebView is alive).
