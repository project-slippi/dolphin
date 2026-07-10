#!/usr/bin/env bash
set -euo pipefail
CONFIG="${1:-Release}"
EMSDK_DIR="${EMSDK_DIR:-$HOME/.slippi-emsdk}"
source "$EMSDK_DIR/emsdk_env.sh"

# Apply web-only patches to submodules (idempotent). These are edits that
# can't live in the superproject; see Tools/web-patches/README.
for spec in "Externals/fmt/fmt:fmt-chrono-consteval.patch" \
            "Externals/SFML/SFML:sfml-emscripten.patch"; do
  sub="${spec%%:*}"; patch="Tools/web-patches/${spec##*:}"
  if git -C "$sub" apply --check "$(pwd)/$patch" 2>/dev/null; then
    git -C "$sub" apply "$(pwd)/$patch"
    echo "applied $patch"
  fi
done

GEN="Unix Makefiles"
if command -v ninja >/dev/null 2>&1; then GEN=Ninja; fi
# CMAKE_POLICY_VERSION_MINIMUM: some Externals declare cmake_minimum_required
# older than modern CMake still accepts.
emcmake cmake -B build-web -DCMAKE_BUILD_TYPE="$CONFIG" -G "$GEN" \
  -DCMAKE_POLICY_VERSION_MINIMUM=3.5
cmake --build build-web --parallel "$(nproc)"
