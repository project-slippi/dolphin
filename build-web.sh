#!/usr/bin/env bash
set -euo pipefail
CONFIG="${1:-Release}"
EMSDK_DIR="${EMSDK_DIR:-$HOME/.slippi-emsdk}"
source "$EMSDK_DIR/emsdk_env.sh"
emcmake cmake -B build-web -DCMAKE_BUILD_TYPE="$CONFIG" -GNinja
cmake --build build-web
