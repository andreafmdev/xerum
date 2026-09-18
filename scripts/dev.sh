#!/usr/bin/env bash
# Avvia tutto in un colpo: @xerum/ui → Vite (5173) → CMake Debug → Standalone.
# Uso: scripts/dev.sh [--web]     (--web: solo browser, niente build JUCE)
# Ctrl-C chiude anche il dev server.
# Questo script builda solo Debug. Le build Release servono XERUM_EMBED_WEBUI (impostato dal preset macos-release) e un WebUI/dist già buildato — vedi docs/build.md.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PORT="${PORT:-5173}"
WEB_ONLY="${1:-}"

cd "$ROOT/WebUI"
[ -d node_modules ] || pnpm install
[ -f packages/ui/dist/index.js ] || pnpm ui:build

if lsof -nP -iTCP:"$PORT" -sTCP:LISTEN >/dev/null 2>&1; then
  echo "▶ porta $PORT già in ascolto: uso quel server (PORT=xxxx per cambiarla)"
  VITE_PID=""
else
  pnpm exec vite --port "$PORT" --strictPort &
  VITE_PID=$!
  trap '[ -n "$VITE_PID" ] && kill "$VITE_PID" 2>/dev/null || true' EXIT
  until curl -fsS "http://localhost:$PORT/" >/dev/null 2>&1; do sleep 0.3; done
fi
echo "▶ UI: http://localhost:$PORT/?variant=deep&tab=env"

if [ "$WEB_ONLY" = "--web" ]; then
  open "http://localhost:$PORT/"
  wait
  exit 0
fi

cd "$ROOT"
[ -f external/JUCE/CMakeLists.txt ] || git submodule update --init --recursive
[ -d build/macos-debug ] || cmake --preset macos-debug
cmake --build --preset macos-debug --target SerumStyleSynth_Standalone

APP="$(ls -d build/macos-debug/SerumStyleSynth_artefacts/Debug/Standalone/*.app | head -1)"
echo "▶ Standalone: $APP"
# Lancio il binario direttamente: `open` non passa l'ambiente, e l'editor legge
# XERUM_WEBUI_URL per puntare a una porta diversa da 5173.
XERUM_WEBUI_URL="http://localhost:$PORT" "$APP/Contents/MacOS/$(basename "$APP" .app)"
