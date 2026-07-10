#!/usr/bin/env bash
set -euo pipefail
VER="$(cat "$(dirname "$0")/emsdk-version.txt")"
EMSDK_DIR="${EMSDK_DIR:-$HOME/.slippi-emsdk}"
if [ ! -d "$EMSDK_DIR" ]; then
  git clone https://github.com/emscripten-core/emsdk.git "$EMSDK_DIR"
fi
cd "$EMSDK_DIR"
git fetch --tags
./emsdk install "$VER"
./emsdk activate "$VER"
echo "emsdk $VER ready. source $EMSDK_DIR/emsdk_env.sh"
