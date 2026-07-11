#!/usr/bin/env bash
# Compares the MEM1 hash after N frames between the native headless-smoke
# build and the wasm build. Exit 0 iff they match.
set -euo pipefail
GAME="$1"; FRAMES="${2:-600}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"

NATIVE_BIN="$ROOT/build-native-smoke/Binaries/dolphin-headless-smoke"
WASM_JS="$ROOT/build-web/Binaries/dolphin-web.js"

# Fresh user dir per run: persisted state (memcards, SRAM, configs) must not
# leak between runs. The wasm side gets a fresh MEMFS /User automatically.
NATIVE_USER=$(mktemp -d)
trap 'rm -rf "$NATIVE_USER"' EXIT

# Hard timeouts: CI must fail, not hang. Runs are captured with `set +e`
# around them: under `set -e` + pipefail, a nonzero harness exit (timeout,
# or an alert firing -> exit 2) would abort the script on the assignment
# itself, before the diagnostic echoes below ever ran, leaving CI logs with
# no indication of which leg failed or why.
set +e
NATIVE_LOG=$(timeout 600 "$NATIVE_BIN" --user "$NATIVE_USER" --exec "$GAME" --frames "$FRAMES" 2>&1)
NATIVE_STATUS=$?
WASM_LOG=$(timeout 1200 node "$ROOT/Tools/run-web-smoke.mjs" "$WASM_JS" "$GAME" "$FRAMES" 2>&1)
WASM_STATUS=$?
set -e

NATIVE_OUT=$(grep MEM1_HASH <<<"$NATIVE_LOG" || true)
WASM_OUT=$(grep MEM1_HASH <<<"$WASM_LOG" || true)

echo "native: exit=$NATIVE_STATUS $NATIVE_OUT"
echo "wasm:   exit=$WASM_STATUS $WASM_OUT"

if [ "$NATIVE_STATUS" -ne 0 ]; then
  echo "native leg failed (exit $NATIVE_STATUS, timeout=600s?) — tail of output:"
  tail -n 20 <<<"$NATIVE_LOG"
  exit 1
fi
if [ "$WASM_STATUS" -ne 0 ]; then
  echo "wasm leg failed (exit $WASM_STATUS, timeout=1200s?) — tail of output:"
  tail -n 20 <<<"$WASM_LOG"
  exit 1
fi
[ -n "$NATIVE_OUT" ] && [ "$NATIVE_OUT" = "$WASM_OUT" ]
