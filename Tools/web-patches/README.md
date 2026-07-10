# Web (Emscripten) submodule patches

Edits needed by the WebAssembly build that live inside git submodules, so they
cannot be committed to this repository directly. `build-web.sh` (and the wasm
CI job) applies them idempotently with `git apply`.

- `fmt-chrono-consteval.patch` — fmt 10.2.1 `write_floating_seconds` uses a
  `FMT_STRING` compile-string lambda that fails consteval checking under
  clang 21 (Emscripten); the plain literal takes the normal consteval path.
  Drop when the fmt submodule is bumped to a release containing the upstream
  fix.
- `sfml-emscripten.patch` — SFML 3.0 `Config.hpp` OS detection errors on
  unknown UNIX; maps `__EMSCRIPTEN__` to the Linux platform path. Drop if
  upstream SFML gains Emscripten detection.

Desktop builds are unaffected whether or not the patches are applied.
