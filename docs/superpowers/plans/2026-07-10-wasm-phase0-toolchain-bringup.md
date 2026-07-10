# WASM Phase 0 — Toolchain Bring-up Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Slippi Dolphin compiles under Emscripten and boots a GC image headless in
Chromium/node on CachedInterpreter + Null video/audio, deterministically, with CI.

**Architecture:** Everything lands behind `if(EMSCRIPTEN)` / `#ifdef __EMSCRIPTEN__`
guards so desktop builds are untouched. CPU = CachedInterpreter (generic path),
video = Null, audio = NullSound, Rust = C stub shim (real crates come later).
Parent spec: `docs/superpowers/specs/2026-07-09-wasm-webgpu-port-design.md`.

**Tech Stack:** emsdk 4.0.10 (pinned), CMake + emcmake, wasm32 + pthreads
(SharedArrayBuffer), `-fwasm-exceptions`, node ≥ 20 for headless smoke runs.

## Global Constraints

- emsdk version: **4.0.10**, pinned in `Tools/emsdk-version.txt`; CI and scripts read it.
- wasm32 only (no MEMORY64), no Asyncify anywhere.
- All flags/pthreads: compile **and** link with `-pthread`; link
  `-sPROXY_TO_PTHREAD -sPTHREAD_POOL_SIZE=16`.
- Never edit desktop behavior: every change guarded by `EMSCRIPTEN` (CMake) or
  `__EMSCRIPTEN__` (C++), or in new files.
- C++ standard stays C++23 (CMakeLists.txt:41). Don't downgrade to dodge errors.
- Commit after every task (messages given per task).
- The existing branch conventions apply — work on a feature branch off `slippi`.

---

### Task 1: Pin the toolchain and add the build script

**Files:**
- Create: `Tools/emsdk-version.txt`
- Create: `Tools/setup-emsdk.sh`
- Create: `build-web.sh` (repo root, sibling of `build-linux.sh`)

**Interfaces:**
- Produces: `build-web.sh [--config Debug|Release]` → configures+builds into `build-web/`.
- Produces: `EMSDK_DIR` convention: `$HOME/.slippi-emsdk` unless `EMSDK_DIR` env set.

- [ ] **Step 1: Write the version pin**

`Tools/emsdk-version.txt`:
```
4.0.10
```

- [ ] **Step 2: Write the setup script**

`Tools/setup-emsdk.sh`:
```bash
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
```

- [ ] **Step 3: Write the build script**

`build-web.sh`:
```bash
#!/usr/bin/env bash
set -euo pipefail
CONFIG="${1:-Release}"
EMSDK_DIR="${EMSDK_DIR:-$HOME/.slippi-emsdk}"
source "$EMSDK_DIR/emsdk_env.sh"
emcmake cmake -B build-web -DCMAKE_BUILD_TYPE="$CONFIG" -GNinja
cmake --build build-web
```

- [ ] **Step 4: Verify**

Run: `chmod +x Tools/setup-emsdk.sh build-web.sh && ./Tools/setup-emsdk.sh && source ~/.slippi-emsdk/emsdk_env.sh && emcc --version | head -1`
Expected: `emcc (Emscripten gcc/clang-like replacement ...) 4.0.10`

(`./build-web.sh` is expected to FAIL at configure until Task 2 — that's fine.)

- [ ] **Step 5: Commit**

```bash
git add Tools/emsdk-version.txt Tools/setup-emsdk.sh build-web.sh
git commit -m "build(web): pin emsdk 4.0.10 and add web build scripts"
```

---

### Task 2: CMake Emscripten platform gate

**Files:**
- Modify: `CMakeLists.txt` (top-level; insert one block right after the `project(...)`
  call and before the first `option(...)` at line ~86, plus one `elseif` in the arch
  detection block at lines 226-253)

**Interfaces:**
- Produces: configuring with the Emscripten toolchain defines `_M_GENERIC=1`,
  `_ARCH_32`, and forces the option set below. Consumed by every later task.

- [ ] **Step 1: Add the forced-options block after `project()`**

```cmake
# slippi change: browser (Emscripten/WebAssembly) platform gate.
# Everything web-specific in the tree is guarded by EMSCRIPTEN (CMake) or
# __EMSCRIPTEN__ (C++). See docs/superpowers/specs/2026-07-09-wasm-webgpu-port-design.md
if(EMSCRIPTEN)
  set(ENABLE_GENERIC ON CACHE BOOL "" FORCE)   # no JIT arch; CachedInterpreter
  foreach(_off
      ENABLE_QT ENABLE_NOGUI ENABLE_CLI_TOOL ENABLE_TESTS
      ENABLE_VULKAN ENABLE_SDL ENABLE_EGL ENABLE_X11
      ENABLE_ALSA ENABLE_PULSEAUDIO ENABLE_CUBEB ENABLE_LLVM
      ENABLE_EVDEV ENABLE_HWDB ENABLE_AUTOUPDATE ENABLE_ANALYTICS
      USE_DISCORD_PRESENCE USE_UPNP USE_MGBA USE_RETRO_ACHIEVEMENTS
      ENCODE_FRAMEDUMPS)
    set(${_off} OFF CACHE BOOL "" FORCE)
  endforeach()
  add_compile_options(-pthread -fwasm-exceptions)
  add_link_options(-pthread -fwasm-exceptions)
endif()
```

- [ ] **Step 2: Handle the arch-detection FATAL_ERROR**

In the architecture block (CMakeLists.txt:226-253): confirm that with
`ENABLE_GENERIC=ON` the wasm32 target reaches the generic branch (`_M_GENERIC=1`,
`_ARCH_32` from `CMAKE_SIZEOF_VOID_P=4`) instead of `FATAL_ERROR`. If the processor
check still trips (empty/`wasm32` `CMAKE_SYSTEM_PROCESSOR`), add:

```cmake
elseif(EMSCRIPTEN)
  # wasm32: generic little-endian ILP32; no host JIT.
  set(_M_GENERIC 1)
  add_definitions(-D_M_GENERIC=1)
```

- [ ] **Step 3: Attempt configure to find the next blocker list**

Run: `./build-web.sh 2>&1 | tail -40`
Expected: configure proceeds past options; it will still FAIL somewhere in
`Externals`/find_library territory. Record every failure — they are Task 3's
work list. Do NOT fix them here.

- [ ] **Step 4: Commit**

```bash
git add CMakeLists.txt
git commit -m "build(web): add Emscripten platform gate to top-level CMake"
```

---

### Task 3: Externals triage — configure completes, vendored libs compile

**Files:**
- Modify: `CMakeLists.txt` (guard `find`/`add_subdirectory` calls that only serve
  disabled features: curl, miniupnpc, libusb/hidapi, FFmpeg, SFML if netplay-only)
- Modify: individual `Externals/*/CMakeLists.txt` only where a define/knob is needed

**Interfaces:**
- Produces: `cmake --build build-web --target <lib>` succeeds for: `fmt`, `zlib-ng`
  (or the tree's zlib target), `zstd`, `lz4`, `xxhash`, `pugixml`, `minizip-ng`,
  `mbedtls`(`mbedcrypto`/`mbedx509`/`mbedtls`), `enet`, `glslang`, `imgui`, `implot`,
  `FreeSurround`, `cpp-optparse`, `expr`, `semver`, `Bochs_disasm` only if `common`
  needs it, SoundTouch/etc. only if pulled in transitively.

- [ ] **Step 1: Guard away the disabled-feature externals**

For each configure failure recorded in Task 2 that belongs to a feature we forced
off, wrap its top-level hookup in `if(NOT EMSCRIPTEN)`. Known candidates from the
options audit: curl (`Common::HttpRequest` gets a stub in Task 6), miniupnpc (UPnP
off), libusb + hidapi (no USB v1), FFmpeg-bin (framedumps off), Discord-rpc, mGBA,
Qt, MoltenVK, SDL, OpenAL, cubeb (audio backends off).

- [ ] **Step 2: Build the keep-list library targets one by one**

Run for each: `cmake --build build-web --target fmt` (then zstd, lz4, …).
Expected: each exits 0. Known knobs if one fails:
- mbedtls: needs `MBEDTLS_NO_PLATFORM_ENTROPY`-style config only if entropy probing
  fails — emscripten provides `getentropy`; prefer no change if it builds clean.
- enet: BSD-socket API compiles against emscripten's libc (`sendmsg`/`recvmsg`
  exist); it is never *invoked* in v1. If a symbol is genuinely missing, guard that
  translation unit's feature with `#ifdef __EMSCRIPTEN__` minimally.
- glslang: disable its SPIRV-Tools optional deps if the tree doesn't vendor them
  (mirror whatever the desktop build already does).

- [ ] **Step 3: Re-run full configure**

Run: `./build-web.sh 2>&1 | tail -5`
Expected: configure completes ("Generating done"); full build still fails inside
`Source/Core` (that's Tasks 4-6).

- [ ] **Step 4: Commit**

```bash
git add CMakeLists.txt Externals
git commit -m "build(web): triage Externals for Emscripten (keep-list compiles)"
```

---

### Task 4: MemArenaEmscripten

**Files:**
- Create: `Source/Core/Common/MemArenaEmscripten.cpp`
- Modify: `Source/Core/Common/CMakeLists.txt:224-249` (platform `target_sources`
  chain: add an `elseif(EMSCRIPTEN)` branch **before** the final `else()` so wasm
  stops compiling `MemArenaUnix.cpp`)

**Interfaces:**
- Consumes: the exact class declaration in `Source/Core/Common/MemArena.h` (read it
  first; implement every member it declares — the list below matches current usage
  from `Memmap.cpp` but the header is the source of truth).
- Produces: a `Common::MemArena` where views are plain pointers into one heap block
  and the fastmem region API always fails (→ Dolphin's existing slowmem path).

- [ ] **Step 1: Read `MemArena.h` and confirm the member list**

Expected members (verify): `GrabSHMSegment(size, base_name)`, `ReleaseSHMSegment()`,
`CreateView(offset, size)`, `ReleaseView(view, size)`, `ReserveMemoryRegion(size)`,
`ReleaseMemoryRegion()`, `MapInMemoryRegion(offset, size, base)`,
`UnmapFromMemoryRegion(view, size)`.

- [ ] **Step 2: Write the implementation**

`Source/Core/Common/MemArenaEmscripten.cpp` (adjust signatures to the header):
```cpp
// Copyright 2026 Dolphin Emulator Project / Slippi
// SPDX-License-Identifier: GPL-2.0-or-later

// wasm32 has one flat linear memory: no shm, no aliased mappings, no page
// protection. The arena is a single heap allocation; every "view" and every
// "mapping" is just a pointer into it (offset-aliasing is therefore exact).
// ReserveMemoryRegion always fails, which routes Dolphin onto its slowmem
// (non-fastmem) path — see Memmap.cpp InitFastmemArena.

#include "Common/MemArena.h"

#include <cstdlib>

#include "Common/Assert.h"
#include "Common/Logging/Log.h"

namespace Common
{
MemArena::MemArena() = default;
MemArena::~MemArena()
{
  ReleaseSHMSegment();
}

void MemArena::GrabSHMSegment(size_t size, std::string_view base_name)
{
  m_backing = static_cast<u8*>(std::calloc(1, size));
  ASSERT_MSG(MEMMAP, m_backing, "MemArenaEmscripten: failed to allocate {} bytes",
             size);
  m_backing_size = size;
}

void MemArena::ReleaseSHMSegment()
{
  std::free(m_backing);
  m_backing = nullptr;
  m_backing_size = 0;
}

void* MemArena::CreateView(s64 offset, size_t size)
{
  ASSERT(m_backing && static_cast<size_t>(offset) + size <= m_backing_size);
  return m_backing + offset;
}

void MemArena::ReleaseView(void* view, size_t size)
{
  // Views are borrowed pointers into m_backing; nothing to do.
}

u8* MemArena::ReserveMemoryRegion(size_t memory_size)
{
  // No virtual-memory tricks on wasm: report failure so callers use slowmem.
  return nullptr;
}

void MemArena::ReleaseMemoryRegion()
{
}

void* MemArena::MapInMemoryRegion(s64 offset, size_t size, void* base)
{
  // Only reachable if ReserveMemoryRegion succeeded — it never does.
  ASSERT_MSG(MEMMAP, false, "MapInMemoryRegion is unsupported on Emscripten");
  return nullptr;
}

void MemArena::UnmapFromMemoryRegion(void* view, size_t size)
{
}
}  // namespace Common
```

Private members `m_backing`/`m_backing_size` go into the `#ifdef __EMSCRIPTEN__`
section of `MemArena.h` alongside the other platforms' private state.

- [ ] **Step 3: Wire up CMake**

In `Source/Core/Common/CMakeLists.txt`, before the final `else()` of the platform
chain:
```cmake
elseif(EMSCRIPTEN)
  target_sources(common PRIVATE
    Logging/ConsoleListenerNix.cpp
    MemArenaEmscripten.cpp
  )
```

- [ ] **Step 4: Compile just this file**

Run: `cmake --build build-web --target common 2>&1 | grep -E 'MemArena|error' | head`
Expected: `MemArenaEmscripten.cpp` compiles; `common` overall may still fail in
*other* files (Task 6's list).

- [ ] **Step 5: Commit**

```bash
git add Source/Core/Common/MemArenaEmscripten.cpp Source/Core/Common/CMakeLists.txt Source/Core/Common/MemArena.h
git commit -m "feat(web): malloc-backed MemArena for Emscripten (slowmem-only)"
```

---

### Task 5: Rust stub shim (`slprs_*`)

**Files:**
- Create: `Source/Core/Common/SlippiRustStubs.c`
- Modify: `Source/Core/Common/CMakeLists.txt:183` area (link the stub instead of
  `slippi_rust_extensions` when `EMSCRIPTEN`)
- Modify: `Source/Core/CMakeLists.txt:41` (skip `corrosion_import_crate` on
  `EMSCRIPTEN` for Phase 0)

**Interfaces:**
- Consumes: prototypes from the FFI header the C++ already includes (find it:
  `grep -r "SlippiRustExtensions.h" Source/Core --include=*.cpp -l` → the include dirs
  are added at `Source/Core/Common/CMakeLists.txt:1` / `Source/Core/Core/CMakeLists.txt:1`).
- Produces: every one of the 40 `slprs_*` symbols the C++ references, as safe no-ops,
  so `common`/`core` link. Later phases replace this with the real workspace built
  for `wasm32-unknown-emscripten` with a `web` cargo feature.

- [ ] **Step 1: Create the stub translation unit**

`Source/Core/Common/SlippiRustStubs.c` — include the same FFI header the C++ uses so
every signature is checked by the compiler, then define all 40 symbols. Pattern
(representative implementations; complete the rest identically, returning `0`,
`NULL`, `false`, or leaving out-params untouched per the header types):

```c
// Phase-0 Emscripten stub for SlippiRustExtensions. Keeps the 40 slprs_*
// symbols link-resolvable while the Rust workspace is not yet cross-compiled.
// Offline (LOCAL) play never exercises these paths meaningfully.
#include "SlippiRustExtensions.h"

uintptr_t slprs_exi_device_create(SlippiRustEXIConfig config) { return 0; }
void slprs_exi_device_destroy(uintptr_t exi_device_instance_ptr) {}
void slprs_logging_init(void) {}
void slprs_user_attempt_login(uintptr_t user_ptr) {}
bool slprs_user_get_is_logged_in(uintptr_t user_ptr) { return false; }
void slprs_jukebox_start_song(uintptr_t exi_ptr, uint64_t offset, uint32_t len) {}
/* ...all remaining slprs_* symbols follow the same shape... */
```

Full symbol checklist (tick each off against the header):
`slprs_exi_config, slprs_exi_device_configure_jukebox, slprs_exi_device_create,
slprs_exi_device_destroy, slprs_exi_device_log_game_report, slprs_exi_device_ptr,
slprs_exi_device_report_match_status, slprs_exi_device_reporter_push_replay_data,
slprs_exi_device_start_new_reporter_session, slprs_fetch_match_result,
slprs_game_report_add_player_report, slprs_game_report_create,
slprs_get_iso_md5_check, slprs_get_rank_info, slprs_jukebox_set_dolphin_music_volume,
slprs_jukebox_set_dolphin_system_volume, slprs_jukebox_set_melee_music_volume,
slprs_jukebox_start_song, slprs_jukebox_stop_music, slprs_logging_init,
slprs_logging_register_container, slprs_logging_update_container,
slprs_mainline_logging_update_log_level, slprs_player_report_create,
slprs_user_attempt_login, slprs_user_direct_codes_add_or_update,
slprs_user_direct_codes_free_code, slprs_user_direct_codes_get_code_at_index,
slprs_user_direct_codes_get_length, slprs_user_free_info, slprs_user_free_messages,
slprs_user_get_default_messages, slprs_user_get_info, slprs_user_get_is_logged_in,
slprs_user_get_messages, slprs_user_listen_for_login, slprs_user_logout,
slprs_user_open_login_page, slprs_user_overwrite_latest_version,
slprs_user_update_app`

Caution: functions returning structs or `char*` (`slprs_user_get_info`,
`slprs_user_get_messages`, `slprs_user_direct_codes_get_code_at_index`, …) must
return a value the C++ callers tolerate — check each call site in
`Source/Core/Core/Slippi/SlippiUser.cpp` / `SlippiDirectCodes.cpp` /
`EXI_DeviceSlippi.cpp` and return an empty-but-valid object (e.g. zeroed struct) —
never an uninitialized one. If a call site dereferences unconditionally, return a
`static` zeroed instance.

- [ ] **Step 2: CMake wiring**

`Source/Core/CMakeLists.txt` around line 41:
```cmake
if(NOT EMSCRIPTEN)
  corrosion_import_crate(MANIFEST_PATH "${CMAKE_SOURCE_DIR}/Externals/SlippiRustExtensions/Cargo.toml" ${RUST_FEATURES})
endif()
```
`Source/Core/Common/CMakeLists.txt` (:183 area):
```cmake
if(EMSCRIPTEN)
  target_sources(common PRIVATE SlippiRustStubs.c)
else()
  target_link_libraries(common PUBLIC slippi_rust_extensions)
endif()
```
(Keep the FFI include dirs in both branches — the stub needs the header too.)

- [ ] **Step 3: Verify it compiles**

Run: `cmake --build build-web --target common 2>&1 | grep -E 'SlippiRustStubs|undefined|error' | head`
Expected: stub compiles; no `slprs_` symbols in any later link error output.

- [ ] **Step 4: Commit**

```bash
git add Source/Core/Common/SlippiRustStubs.c Source/Core/Common/CMakeLists.txt Source/Core/CMakeLists.txt
git commit -m "build(web): stub slprs_* FFI surface for Phase 0 (no Rust yet)"
```

---

### Task 6: Core compile sweep (`common`→`core`→`videocommon` green)

**Files:**
- Modify: scattered small guards. Known work items (from exploration; the build log
  is the authoritative list):
  - `Source/Core/VideoBackends/CMakeLists.txt:1-3` — OGL requires GL context/EGL:
    wrap `add_subdirectory(OGL)` in `if(NOT EMSCRIPTEN)`; keep `Null` and `Software`.
  - `Source/Core/VideoCommon/VideoBackendBase.cpp:194-214` — guard the OGL
    registration with `#if defined(HAS_OPENGL)` if not already; ensure Null+Software
    register on Emscripten.
  - `Common::HttpRequest` (curl): add `Source/Core/Common/HttpRequestStub.cpp`
    compiled only on EMSCRIPTEN implementing the public methods of
    `Common/HttpRequest.h` to return failure (`std::nullopt`), and exclude
    `HttpRequest.cpp`; callers (AutoUpdate etc.) already handle failure.
  - `Source/Core/InputCommon/GCAdapter.cpp` — CMake-gate behind libusb presence
    (pattern already exists for other optional deps); provide the existing no-adapter
    code path (`GCAdapter` has `#if defined(...)` structure for Android already —
    reuse that shape).
  - `Source/Core/Core/HW/EXI/EXI_DeviceSlippi.cpp` + `Source/Core/Core/Slippi/` —
    should compile as-is (threads are std::thread, files are IOFile). Only gate the
    spectate server *start* (`SlippiSpectate.cpp:173` thread spawn) with
    `#ifndef __EMSCRIPTEN__` — port binding at construction otherwise aborts at
    runtime later.
  - `MemoryWatcher`/evdev/udev/X11 units — already excluded by the forced-off
    options; if one leaks into the build, gate its `target_sources` the same way.
- Create: `Source/Core/Common/HttpRequestStub.cpp` (above)

**Interfaces:**
- Produces: `cmake --build build-web --target core videocommon videonull
  videosoftware inputcommon audiocommon uicommon` all exit 0.

- [ ] **Step 1: Iterate on `common` until green**

Run: `cmake --build build-web --target common 2>&1 | grep -m5 error`
Fix each error with the narrowest possible `#ifdef __EMSCRIPTEN__` or CMake guard,
following the known-items list. Re-run until exit 0.

- [ ] **Step 2: Same for `videocommon`, `audiocommon`, `inputcommon`, `discio`, `core`, `uicommon`**

Run each target in that order (dependency order keeps error lists short).
Expected: exit 0 each. ILP32 warnings (`-Wshorten-64-to-32`-class) are fine to leave
as warnings this phase; fix only hard errors.

- [ ] **Step 3: Commit (may be several commits during iteration — one per subsystem is ideal)**

```bash
git add -A Source/Core
git commit -m "build(web): core libraries compile under Emscripten"
```

---

### Task 7: DolphinWeb skeleton — headless boot

**Files:**
- Create: `Source/Core/DolphinWeb/CMakeLists.txt`
- Create: `Source/Core/DolphinWeb/MainWeb.cpp`
- Create: `Source/Core/DolphinWeb/HostWeb.cpp`
- Modify: `Source/Core/CMakeLists.txt` (add `add_subdirectory(DolphinWeb)` under
  `if(EMSCRIPTEN)`)

**Interfaces:**
- Consumes: `UICommon::Init/Shutdown/SetUserDirectory`, `UICommon::InitControllers`,
  `BootManager::BootCore`, `Core::HostDispatchJobs`, `Core::Stop`,
  `Core::QueueHostJob` — exactly as sequenced in `DolphinNoGUI/MainNoGUI.cpp:217-360`.
- Produces: `dolphin-web.{js,wasm}` that boots `/game.iso|.dol` from the Emscripten
  FS, runs N frames, prints `SLIPPI_WEB_BOOTED` and per-run `SLIPPI_WEB_MEM1_HASH=…`.
  The CLI contract (`?frames=N` / argv `--frames N --hash`) is Task 8's input.

- [ ] **Step 1: Write HostWeb.cpp**

Copy the declaration list from `Source/Core/Core/Host.h:51-85` (24 functions) and
give each the NoGUI-equivalent trivial body (see `MainNoGUI.cpp:56-179`): booleans
return the NoGUI defaults (`Host_RendererHasFocus()` → `true`, etc.),
`Host_Message(HostMessageID id)` mirrors NoGUI's shutdown-request handling, the rest
are empty. Every body that NoGUI implements non-trivially gets the same logic minus
the platform object (store the running/shutdown flags in file-local atomics).

- [ ] **Step 2: Write MainWeb.cpp**

Structure (full boot sequence per the Interfaces block; `main` runs on a pthread via
PROXY_TO_PTHREAD so blocking is fine):

```cpp
// Phase-0 headless entry: boot a game from the Emscripten FS, run N frames,
// hash MEM1, exit. Grows into the embind frontend in Phase 3.
#include <cstdio>
#include <thread>
#include "Common/Hash.h"
#include "Core/Boot/Boot.h"
#include "Core/BootManager.h"
#include "Core/Core.h"
#include "Core/HW/Memmap.h"
#include "Core/System.h"
#include "UICommon/UICommon.h"

int main(int argc, char* argv[])
{
  // args: --exec <path> [--frames N]
  std::string path = "/game.iso";
  int frames = 600;
  // (plain argv parse; emscripten passes Module.arguments)

  UICommon::SetUserDirectory("/User");  // MEMFS for Phase 0; OPFS in Phase 3
  UICommon::Init();
  UICommon::InitControllers(WindowSystemInfo(WindowSystemType::Headless,
                                             nullptr, nullptr, nullptr));

  auto& system = Core::System::GetInstance();
  if (!BootManager::BootCore(system, BootParameters::GenerateFromFile(path),
                             WindowSystemInfo(WindowSystemType::Headless, nullptr,
                                              nullptr, nullptr)))
  {
    std::fprintf(stderr, "SLIPPI_WEB_BOOT_FAILED\n");
    return 1;
  }
  std::printf("SLIPPI_WEB_BOOTED\n");

  WaitForFrames(system, frames);  // Task 8 defines this precisely

  const u64 hash = HashMem1(system);  // Task 8
  std::printf("SLIPPI_WEB_MEM1_HASH=%016llx\n", (unsigned long long)hash);

  Core::Stop(system);
  Core::Shutdown(system);
  UICommon::Shutdown();
  return 0;
}
```
For this task, `WaitForFrames` may be a plain
`std::this_thread::sleep_for(std::chrono::seconds(10))` and `HashMem1` may return 0
— Task 8 replaces both with the real frame-counted versions. Config forced at boot
via the layered config system: CPU core = CachedInterpreter, video backend = `Null`,
audio = `No Audio Output`, dual core = false (single-core for determinism),
fastmem = false. Set these through `Config::SetBaseOrCurrent` before `BootCore`
(mirror how MainNoGUI applies `movie`/config options).

- [ ] **Step 3: Write DolphinWeb/CMakeLists.txt**

```cmake
add_executable(dolphin-web MainWeb.cpp HostWeb.cpp)
target_link_libraries(dolphin-web PRIVATE core uicommon)
set_target_properties(dolphin-web PROPERTIES SUFFIX ".js")
target_link_options(dolphin-web PRIVATE
  -pthread
  -sPROXY_TO_PTHREAD
  -sPTHREAD_POOL_SIZE=16
  -sINITIAL_MEMORY=805306368      # 768 MiB
  -sALLOW_MEMORY_GROWTH
  -sMAXIMUM_MEMORY=2147483648     # 2 GiB
  -sSTACK_SIZE=2097152
  -sDEFAULT_PTHREAD_STACK_SIZE=2097152
  -sENVIRONMENT=web,worker,node
  -sEXIT_RUNTIME
  --preload-file "${CMAKE_SOURCE_DIR}/Data/Sys@/Sys"
)
```

- [ ] **Step 4: Build and smoke-boot a free homebrew .dol under node**

Melee can't live in CI; use a redistributable homebrew GC .dol for smoke (keep it
out of the repo: `Tools/fetch-test-dol.sh` downloads a pinned-URL,
pinned-SHA256 homebrew binary into `build-web/`). Then:

Run:
```bash
node build-web/Binaries/dolphin-web.js --exec /game.dol --frames 60 \
  # game.dol injected via Module.preRun FS.writeFile in a 10-line runner:
node Tools/run-web-smoke.mjs build-web/Binaries/dolphin-web.js build-web/test.dol
```
(`run-web-smoke.mjs`: loads the emitted JS, sets `Module.preRun` to write the .dol
into MEMFS at `/game.dol`, sets `Module.arguments = ['--exec','/game.dol']`, awaits
exit code.)
Expected output contains: `SLIPPI_WEB_BOOTED`.

- [ ] **Step 5: Local (non-CI) Melee verification**

Run the same runner against a local Melee 1.02 ISO path.
Expected: `SLIPPI_WEB_BOOTED` and no abort for 600 frames.

- [ ] **Step 6: Commit**

```bash
git add Source/Core/DolphinWeb Source/Core/CMakeLists.txt Tools/run-web-smoke.mjs Tools/fetch-test-dol.sh
git commit -m "feat(web): DolphinWeb headless frontend boots under node/browser"
```

---

### Task 8: Determinism smoke (frame-counted MEM1 hash, native vs wasm)

**Files:**
- Modify: `Source/Core/DolphinWeb/MainWeb.cpp` (real `WaitForFrames`/`HashMem1`)
- Create: `Tools/wasm-determinism.sh`
- Modify: `Source/Core/DolphinWeb/CMakeLists.txt` (also build `dolphin-web` as a
  native `dolphin-headless-smoke` when NOT EMSCRIPTEN — same two .cpp files, so the
  identical harness runs on desktop; gate in `Source/Core/CMakeLists.txt` behind a
  new `ENABLE_HEADLESS_SMOKE` option default OFF)

**Interfaces:**
- Consumes: a frame-count source. Candidates in order: `Movie::GetCurrentFrame()`
  (Core/Movie.h) when playing a DTM; otherwise the VI field/frame counter
  (Core/HW/VideoInterface.h — locate the accessor; `Host_UpdateMainFrame` cadence is
  a fallback). Pick ONE, use it for both native and wasm builds.
- Produces: `SLIPPI_WEB_MEM1_HASH=<16 hex>` after exactly N emulated frames;
  `Tools/wasm-determinism.sh` exits 0 iff native and wasm hashes match.

- [ ] **Step 1: Implement WaitForFrames + HashMem1**

`WaitForFrames`: poll the chosen frame counter every 5 ms from the main pthread
until `>= N` (emulation runs unthrottled: set the speed limit config to 0 /
unlimited for the run). `HashMem1`: pause the core
(`Core::SetState(Core::State::Paused)`), then
`XXH64(memory.GetRAM(), memory.GetRamSizeReal(), 0)` via the tree's hash utility
(`Common/Hash.h` — use the same function on both builds).

- [ ] **Step 2: Add the native twin target and the diff script**

`Tools/wasm-determinism.sh`:
```bash
#!/usr/bin/env bash
set -euo pipefail
GAME="$1"; FRAMES="${2:-600}"
NATIVE_OUT=$(./build-native-smoke/Binaries/dolphin-headless-smoke --exec "$GAME" --frames "$FRAMES" | grep MEM1_HASH)
WASM_OUT=$(node Tools/run-web-smoke.mjs build-web/Binaries/dolphin-web.js "$GAME" "$FRAMES" | grep MEM1_HASH)
echo "native: $NATIVE_OUT"; echo "wasm:   $WASM_OUT"
[ "$NATIVE_OUT" = "$WASM_OUT" ]
```
Native twin build: `cmake -B build-native-smoke -DENABLE_HEADLESS_SMOKE=ON
-DENABLE_QT=OFF -DENABLE_NOGUI=OFF` + force CachedInterpreter/Null/single-core in
the harness code itself (same code path as wasm — the config forcing from Task 7
Step 2 lives in MainWeb.cpp, shared by both targets).

- [ ] **Step 3: Run it**

Run: `./Tools/wasm-determinism.sh build-web/test.dol 600`
Expected: both lines print, script exits 0. Then locally with Melee + 600 frames:
exit 0. If hashes diverge: that's a real ILP32/portability bug — bisect by halving
N; fix before proceeding (this gate is the whole point of Phase 0).

- [ ] **Step 4: Commit**

```bash
git add Source/Core/DolphinWeb Tools/wasm-determinism.sh Source/Core/CMakeLists.txt
git commit -m "test(web): native-vs-wasm MEM1 determinism harness"
```

---

### Task 9: CI workflow

**Files:**
- Create: `.github/workflows/wasm.yml`
- Create: `docs/WebBuild.md` (2-paragraph how-to: setup
  script, build script, smoke commands, COOP/COEP note for serving in a browser)

**Interfaces:**
- Consumes: everything above; `Tools/emsdk-version.txt` as the single version source.
- Produces: PR-gating job `wasm-build` (build + node smoke + determinism on the
  homebrew .dol).

- [ ] **Step 1: Write the workflow**

```yaml
name: wasm
on:
  pull_request:
  push:
    branches: [slippi]
jobs:
  wasm-build:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
        with: { submodules: recursive }
      - name: Read emsdk pin
        id: pin
        run: echo "ver=$(cat Tools/emsdk-version.txt)" >> "$GITHUB_OUTPUT"
      - uses: mymindstorm/setup-emsdk@v14
        with:
          version: ${{ steps.pin.outputs.ver }}
          actions-cache-folder: emsdk-cache
      - uses: hendrikmuhs/ccache-action@v1
        with: { key: wasm }
      - name: Configure and build (wasm)
        run: |
          emcmake cmake -B build-web -GNinja -DCMAKE_BUILD_TYPE=Release \
            -DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache
          cmake --build build-web
      - name: Configure and build (native smoke twin)
        run: |
          cmake -B build-native-smoke -GNinja -DCMAKE_BUILD_TYPE=Release \
            -DENABLE_HEADLESS_SMOKE=ON -DENABLE_QT=OFF -DENABLE_NOGUI=OFF
          cmake --build build-native-smoke --target dolphin-headless-smoke
      - name: Smoke + determinism
        run: |
          ./Tools/fetch-test-dol.sh build-web/test.dol
          ./Tools/wasm-determinism.sh build-web/test.dol 600
      - uses: actions/upload-artifact@v4
        with:
          name: dolphin-web
          path: build-web/Binaries/dolphin-web.*
```

- [ ] **Step 2: Verify on a PR**

Push the branch, open a draft PR, confirm the `wasm-build` job is green end-to-end.
Expected: green check; artifact `dolphin-web` downloadable.

- [ ] **Step 3: Commit**

```bash
git add .github/workflows/wasm.yml docs/WebBuild.md
git commit -m "ci(web): wasm build + determinism smoke on PRs"
```

---

## Phase 0 exit criteria (from the spec)

1. `./build-web.sh` produces `dolphin-web.js/.wasm` from a clean checkout.
2. Node/browser headless boot prints `SLIPPI_WEB_BOOTED` for homebrew .dol and (locally) Melee.
3. `Tools/wasm-determinism.sh` exit 0 at 600 frames for both.
4. CI job green on PRs; desktop builds provably untouched (existing CI still green).

## What Phase 0 explicitly defers

Real Rust crates under wasm (`web` cargo feature + corrosion
`wasm32-unknown-emscripten`), JitWasm (Phase 1), WebGPU/naga (Phase 2), audio/input/
OPFS/embind app (Phase 3), any netplay. See the parent spec's phase table.
