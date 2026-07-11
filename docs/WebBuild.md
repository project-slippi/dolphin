# WebAssembly (browser) build

Phase 0 of the browser port: the full Slippi Dolphin core compiles under
Emscripten and boots headless (CachedInterpreter + Null video/audio). See
`docs/superpowers/specs/2026-07-09-wasm-webgpu-port-design.md` for the roadmap.

## Building

```bash
./Tools/setup-emsdk.sh          # one-time: installs the pinned emsdk
./build-web.sh                  # emcmake configure + build into build-web/
```

Artifacts land in `build-web/Binaries/`: `dolphin-web.js`, `dolphin-web.wasm`,
`dolphin-web.data` (packaged `Data/Sys`).

The build applies small web-only patches to the fmt/SFML submodules
(`Tools/web-patches/`, idempotent `git apply`).

## Headless smoke run (node)

```bash
node Tools/run-web-smoke.mjs build-web/Binaries/dolphin-web.js <game> [fields]
```

Boots the game, runs `fields` VI fields unthrottled, prints
`SLIPPI_WEB_BOOTED` and `SLIPPI_WEB_MEM1_HASH=<hash>`.

## Determinism check (native vs wasm)

```bash
cmake -B build-native-smoke -GNinja -DCMAKE_BUILD_TYPE=Release \
  -DENABLE_HEADLESS_SMOKE=ON -DENABLE_QT=OFF -DENABLE_NOGUI=OFF -DENABLE_TESTS=OFF
cmake --build build-native-smoke --target dolphin-web
./Tools/wasm-determinism.sh <game> [fields]
```

Runs the identical harness natively and under node and compares the MEM1 hash
after the same number of emulated fields (fixed RTC, fresh user dir). Exit 0
iff identical.

## Browser serving note

pthreads require SharedArrayBuffer, which requires cross-origin isolation:
serve with `Cross-Origin-Opener-Policy: same-origin` and
`Cross-Origin-Embedder-Policy: require-corp`.
