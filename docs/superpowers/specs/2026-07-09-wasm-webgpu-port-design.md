# Slippi Dolphin → WebAssembly (browser) with WebGPU — Design

**Date:** 2026-07-09
**Status:** Approved decisions baked in; inventory sections pending final exploration pass are marked.

## Context

Slippi Dolphin (this repo: mainline Dolphin fork + Slippi rollback netplay in
`Source/Core/Core/Slippi/`, `Source/Core/Core/HW/EXI/EXI_DeviceSlippi.*`) ships as a
desktop app. Goal: run the emulator in the browser so people can play Melee without
installing anything — eventually including Slippi netplay, starting with offline play.

### Decisions (fixed)

| Axis | Decision |
|---|---|
| v1 scope | **Offline play**: boot Melee in Chromium, local human/CPU players. Netplay is designed-for (clean transport seam) but implemented in a later phase. |
| Graphics | **Rust wgpu stack**: new C++ `VideoBackends/WebGPU` written against `webgpu.h`. Browser build links **emdawnwebgpu** (the browser *is* the WebGPU implementation — Rust wgpu cannot run inside an Emscripten module because its web backend requires wasm-bindgen). The Rust wgpu ecosystem is still load-bearing: **wgpu-native** implements the same backend for native dev/debug builds, and **naga** (compiled to wasm via the existing Rust workspace) does runtime SPIR-V→WGSL shader translation in the shipped artifact. |
| CPU core | **JitWasm required for v1** — a PPC→wasm JIT (v86-style runtime wasm codegen). CachedInterpreter remains as tier-0/fallback. Full-speed Melee (60 FPS) is the v1 acceptance bar. |
| Browser floor | **Chromium-only v1**: SharedArrayBuffer+pthreads (COOP/COEP), WebGPU in a worker with OffscreenCanvas, OPFS, sync wasm compile in workers. Firefox/Safari widening is a later phase. |

### Non-goals for v1

- Netplay (ranked/unranked/direct), spectating, matchmaking — later phase (WebRTC).
- GC adapter (WebUSB) — later phase.
- Jukebox, playback/replay-viewer build (`SLIPPI_PLAYBACK`) — later phases (the replay
  viewer is a strong v1.5 candidate: pure offline, high value).
- Firefox/Safari, mobile browsers, Wii titles, wasm64.

## Feasibility anchors (verified in-tree)

- `ENABLE_GENERIC` already exists (CMakeLists.txt:105) — the codebase supports JIT-less
  little-endian generic builds; `PowerPC::AvailableCPUCores()`
  (Source/Core/Core/PowerPC/PowerPC.cpp:220) already falls back to
  CachedInterpreter/Interpreter on non-x86/ARM, and `DefaultCPUCore()` returns
  CachedInterpreter there.
- Fastmem is optional: `Memory::InitFastmemArena` (Source/Core/Core/HW/Memmap.cpp:204)
  tolerates reservation failure; JitArm64 already runs a no-fastmem-arena path
  (Source/Core/Core/PowerPC/JitInterface.cpp:92). The MemArena backends are per-platform
  files (`Common/MemArena{Unix,Win,Darwin,Android}.cpp`) — a `MemArenaEmscripten.cpp` is
  small: one malloc'd block; "views" are pointers into it (aliasing is free because all
  views share the same backing block).
- Shader generation already produces SPIR-V at runtime via glslang for Vulkan
  (Source/Core/VideoBackends/Vulkan/ShaderCompiler.cpp) — the input side of the
  naga SPIR-V→WGSL path exists.
- Rust is already integrated via corrosion (CMakeLists.txt:631) with a workspace at
  `Externals/SlippiRustExtensions/` (`panic = "abort"` already set — required for wasm).
- CachedInterpreter (Source/Core/Core/PowerPC/CachedInterpreter/) is a complete portable
  JitBase implementation with its own block cache — the structural template for JitWasm.
  It emits to non-executable memory (`CodeBlock<CachedInterpreterEmitter, false>`,
  CachedInterpreterEmitter.h:102) — no PROT_EXEC anywhere in a generic build.
- Generic fallbacks exist at every arch-specific site: VertexLoader falls back to the
  software loader when neither x64 nor ARM64 (VertexLoaderBase.cpp:258-264), the
  fastmem fault handler is a no-op under `_M_GENERIC` (MemTools.cpp:372-380), SIMD in
  Common/Hash, Crypto, CPUCull is all `#ifdef`-guarded with scalar paths, and the
  fastmem arena is only ever reserved by Jit64/JitArm64 — a CachedInterpreter build
  never calls `InitFastmemArena`.
- Bounding box degrades softly when a backend reports no support
  (VideoCommon/BoundingBox.cpp:32,60,79 early-return) — a first-pass WebGPU backend can
  ship without it and add a storage-buffer implementation later.
- C++23 (CMakeLists.txt:41) — fine on current emsdk clang; pin the emsdk version.

Two hard constraints the port must clear early (details in risks):
- **Rust is a hard dependency of everything**: `slippi_rust_extensions` is linked
  PUBLIC into `common` (Source/Core/Common/CMakeLists.txt:183, imported at
  Source/Core/CMakeLists.txt:41) — the C++ side calls 40 `slprs_*` symbols
  unconditionally. Phase 0 satisfies the linker with a C stub shim; real crates follow.
- **wasm32 is an ILP32 target**, and the build system currently `FATAL_ERROR`s on
  unknown/32-bit-pointer arches unless `ENABLE_GENERIC` (CMakeLists.txt:226-253, with
  an explicit "this'll break" comment about 32-bit pointers). Expect latent
  pointer-size assumptions; the determinism harness is the safety net.

## Architecture

### Process model (browser)

```
Browser main thread (JS/TS host app — no wasm on it)
 ├─ UI shell, file import (ISO → OPFS), config UI
 ├─ Input capture: keyboard events + Gamepad API poll → SAB input block
 └─ spawns workers:
     ├─ emu-main pthread (PROXY_TO_PTHREAD): DolphinWeb init, BootManager, Host loop
     │    ├─ CPU thread (pthread/worker): JitWasm dispatcher (+ CachedInterpreter tier-0)
     │    ├─ Video thread (pthread/worker): VideoBackends/WebGPU on OffscreenCanvas
     │    │     (worker-local requestAnimationFrame; emdawnwebgpu → browser WebGPU)
     │    ├─ JIT compile thread: wasm module assembly + WebAssembly.instantiate
     │    └─ aux pthreads (shader precompile pool, disc prefetch)
     └─ AudioWorklet: pulls samples from SAB ring buffer fed by the Mixer
```

- One Emscripten module, wasm32, pthreads (SAB), initial 768 MiB / max 2 GiB growable.
- Static hosting + COOP/COEP headers (required for SAB). No server component in v1.
- OPFS holds the User directory (config INIs, .slp replays, savestates) and the
  imported game image; the CPU/IO threads read the ISO via OPFS sync access handles
  (real random-access reads off-main-thread — fits DiscIO's chunked/seek read model).

### New top-level pieces

| Piece | Location | Role |
|---|---|---|
| Emscripten platform gate | top CMakeLists.txt + `CMake/` | toolchain detection, forced options (Qt/Vulkan/SDL/curl/enet/discord/mGBA/FFmpeg off), `EMSCRIPTEN` platform branch |
| `MemArenaEmscripten.cpp` | `Source/Core/Common/` | malloc-backed arena; `ReserveMemoryRegion` returns null → slowmem path |
| `JitWasm` | `Source/Core/Core/PowerPC/JitWasm/` | PPC→wasm JIT: new `CPUCore::JITWasm` (emscripten-only), JitBase subclass |
| `VideoBackends/WebGPU` | `Source/Core/VideoBackends/WebGPU/` | webgpu.h backend; emdawnwebgpu in browser, wgpu-native native |
| `naga-ffi` crate | `Externals/SlippiRustExtensions/naga-ffi/` | C ABI: SPIR-V bytes in → WGSL string out (naga `spv-in`,`wgsl-out`) |
| `EmscriptenAudioStream` | `Source/Core/AudioCommon/` | SoundStream over Emscripten Audio Worklets + SAB ring |
| ControllerInterface backend | `Source/Core/InputCommon/ControllerInterface/Emscripten/` | keyboard + Gamepad API devices (state via SAB from main thread) |
| `DolphinWeb` frontend | `Source/Core/DolphinWeb/` | embind API (init/importedGameBoot/stop/padState/config), Host_* impls, PROXY_TO_PTHREAD main |
| Web host app | `WebDist/` | TS static site, COOP/COEP dev server, import/config/canvas UI |

### Subsystem designs

#### 1. CPU: JitWasm (v1 flagship, highest risk — Phase 1)

Precedent: v86 (x86 PC emulator) proves the architecture — runtime-generate wasm
modules for hot code, instantiate asynchronously, install into a function table,
keep an interpreter tier for cold/new code.

> **Correction (2026-07-10 external review):** WebAssembly tables are
> *agent-local* — each pthread worker has its own instance and function table;
> only linear memory is shared. A compiler worker therefore **cannot** install
> functions into the CPU worker's table. Publication must be two-phase:
> compile `WebAssembly.Module` bytes anywhere (module objects are structured-
> cloneable postMessage payloads), but **instantiate + table-install must run
> on the CPU worker itself**, at an explicit safe point where the CPU run loop
> yields to its worker event loop (CachedInterpreter's loop never does today —
> a periodic `emscripten_yield`-style rendezvous or message-drain point must be
> added). Builds must enable `-sALLOW_TABLE_GROWTH` (default off). Table slots
> need generation counters for invalidation, with dead entries cleared. This
> exact mechanism is a **Phase 0.5 spike** (see phase table) and is the go/no-go
> gate for starting JitWasm proper.

- **Structure:** `JitWasm : JitBase` mirroring CachedInterpreter (own block-cache
  subclass, same `Run()/Jit(u32)/ClearCache()` surface). New `CPUCore::JITWasm` enum
  value gated `#ifdef __EMSCRIPTEN__`; `DefaultCPUCore()` returns it there.
- **Tiering:** first execution of a block runs via CachedInterpreter machinery and
  enqueues the block; the compile thread batches ~64–256 analyzed blocks into one wasm
  module (custom in-tree wasm-binary emitter, ~1–2k lines, no external deps), calls
  `WebAssembly.instantiate` (async, off the CPU thread), then installs exported
  functions into the module's function table (`wasmTable.grow()` + set). Emscripten
  function pointers *are* table indices, so a compiled block is callable from C++ as a
  plain function pointer of signature `i32 block(i32 ppcstate)` — the dispatcher stays
  pure C++ (fast_block_map lookup → call, exactly like CachedInterpreter's loop).
- **Codegen:** GPR/FPR values in wasm locals within a block, flushed to `ppcState` (at a
  fixed linear-memory address, imported shared memory) at exits/slow calls. Paired
  singles via scalar f32 pairs first; wasm simd128 as a later optimization. Downcount
  maintained in a local, checked at exits/backedges (CoreTiming unchanged).
- **Memory access:** no signal-handler fastmem on wasm. Inline the software-BAT fast
  path (the JitArm64 no-arena mode is the model): BAT-table lookup (two loads) → direct
  `i32.load` at `ram_base + phys`; MMIO/uncached falls back to an imported C++ helper.
- **Invalidation/linking:** cross-block transfers return to the dispatcher in v1 (no
  direct block→block links across modules; intra-module direct calls allowed for
  co-batched blocks). `InvalidateICache` marks table slots dead and drops module refs;
  Melee's code churn is low (boot-time Gecko patches, occasional loads).
- **Determinism oracle (the key Slippi advantage):** replay driven RAM-hash lockstep —
  play identical inputs (DTM movie first; .slp playback once the playback code is in)
  on native CachedInterpreter vs wasm builds and compare per-frame xxhash of work RAM.
  Also per-instruction lockstep mode vs Interpreter for JIT bring-up.
- **Go/no-go gate:** Phase 1 exits only when in-game Melee holds 60 VPS on the
  reference machine with Null video/audio. Escalation ladder if short: megamodule
  recompilation of hot regions with internal direct calls → simd128 paired singles →
  profile-guided block layout. Fallback config always available: CachedInterpreter.

#### 2. Graphics: VideoBackends/WebGPU on the wgpu stack (Phase 2)

- Implements the VideoCommon abstract interface — the Metal backend
  (`Source/Core/VideoBackends/Metal/`, ~4,600 lines total) is the structural template
  (single modern API, no legacy paths; the bulk is MTLStateTracker at ~1,300 lines and
  object caching at ~700). Required surface:
  - `VideoBackendBase` (VideoBackendBase.h:33): `Initialize(WindowSystemInfo)`,
    `Shutdown`, `GetName`, `InitBackendInfo`; register in
    VideoBackendBase.cpp:194-214 and Source/Core/VideoBackends/CMakeLists.txt.
  - `AbstractGfx` (AbstractGfx.h:49) pure: `IsHeadless`, `CreateTexture`,
    `CreateStagingTexture`, `CreateFramebuffer`, `CreateShaderFromSource/Binary`,
    `CreateNativeVertexFormat`, `CreatePipeline`, `GetSurfaceInfo`; plus the ~40
    non-pure draw/state hooks (SetPipeline, Draw/DrawIndexed, Bind/PresentBackbuffer,
    DispatchComputeShader…). Object graph wiring: VideoBackendBase.cpp:260-319.
  - `AbstractTexture` (Load/Copy/Resolve), `AbstractStagingTexture` (Map/Unmap/Flush +
    copies), `AbstractShader`/`AbstractPipeline` (thin), `VertexManagerBase`
    (ResetBuffer/CommitBuffer/UploadUniforms/DrawCurrentBatch/UploadTexelBuffer).
  - `PerfQueryBase` defaults are no-ops (ship that first); BoundingBox optional (see
    feasibility anchors).
  - New enum members required: `APIType::WebGPU` (VideoCommon/VideoCommon.h:38 — shader
    gen, Spirv.cpp:58-59 Vulkan-rules gating, pipeline caching all key off it) and
    `WindowSystemType::Emscripten` (Common/WindowSystemInfo.h:6-16 — canvas selector
    rides in `wsi.render_surface`).
- **Shaders:** the Metal backend already demonstrates the exact seam
  (MTLUtil.mm:493 `TranslateShaderToMSL`): generate GLSL → `SPIRV::Compile*Shader`
  (VideoCommon/Spirv.h:23-36, glslang) → *spirv_cross→MSL*. The WebGPU backend replaces
  the last hop with `naga-ffi` SPIR-V→WGSL at pipeline-build time, cached by the
  existing shader-UID system. Specialized shaders + async compile via the existing
  VideoCommon shader cache; exclusive-ubershader mode is out of scope for v1.
- **Two implementations, one header:** browser = emdawnwebgpu port
  (`emcc --use-port=emdawnwebgpu`; the old `-sUSE_WEBGPU` was removed upstream
  Nov 2025); native dev builds = **wgpu-native** staticlib built by corrosion behind a
  new `ENABLE_WGPU` CMake option, so the whole backend is developed and FifoCI-style
  tested natively before any browser wiring exists. Known risk: wgpu-native currently
  lags the stable webgpu.h; isolate all divergence in one `WebGPUCompat.h` shim and pin
  both versions. Fallback if the skew is too painful: link Dawn for the native dev loop
  instead (backend code unchanged — same header family).
- **Presentation:** video thread owns the OffscreenCanvas surface; worker-side rAF
  drives presentation; emulation free-runs off CoreTiming (display Hz ≠ 60 handled by
  decoupled present, as on desktop).

> **Correction (2026-07-10 external review):** the GPU/video thread blocks in
> `RunGpuLoop()` until shutdown (VideoCommon/Fifo.cpp:288), but browser WebGPU
> is event-loop driven — adapter/device acquisition, buffer-map completion, and
> worker rAF callbacks cannot fire while the worker spins in wasm. The blanket
> "no Asyncify anywhere" rule therefore needs one of: (a) refactor the video
> thread's loop into an event-driven state machine that returns to its event
> loop, (b) a narrowly scoped JSPI/Asyncify path around the blocking waits, or
> (c) a dedicated event-driven WebGPU worker behind an RPC layer. Decide in the
> **Phase 0.5 spike** (adapter acquisition + buffer map on a pthread without
> deadlock). Canvas ownership is likewise unresolved: PROXY_TO_PTHREAD only
> moves main() to a pthread; Dolphin then spawns a *different* emu/video thread
> (Core.cpp:254), so the OffscreenCanvas must be explicitly transferred to that
> thread (or the thread topology changed) — prototype this in the same spike.
- **EFB readbacks:** WebGPU buffer maps are async-only. CPU EFB peeks/pokes block the
  CPU pthread on a future while the GPU worker pumps — legal off-main-thread. Melee
  gameplay does not depend on EFB peeks, so this is correctness plumbing, not a perf
  path. Verify future-wait semantics under emdawnwebgpu in the Phase 2 spike.
- Bounding box: WebGPU storage buffers cover it; Melee doesn't use BBox, implement
  late.

#### 3. Memory (Phase 0)

- `MemArenaEmscripten.cpp`: `GrabSHMSegment` → aligned heap block; `CreateView`/
  `MapInMemoryRegion(offset,…)` → `block + offset`; `ReserveMemoryRegion` → nullptr
  (fastmem arena disabled; MMU slowmem path takes over). Config forces
  `MAIN_FASTMEM=false` on emscripten.
- Budget (verified in Memmap.cpp:86-124): the GC shared segment is ~64.25 MiB
  (MEM1 rounded to 32 MiB + 256 KiB L1 + 32 MiB fake VMEM; MEM2 is Wii-only).
  CachedInterpreter's code buffer is the largest single allocation at 128 MiB
  (`CODE_SIZE`, Jit64Common/Jit64Constants.h:23 — shrinkable on wasm if needed).
  The Slippi rollback savestate pool (~7-8 MiB × ROLLBACK_MAX_FRAMES ≈ 50-60 MiB,
  SlippiSavestate.cpp:28,54-65) only materializes for online matches — not paid in v1.
  Total v1 working set is on the order of 300 MiB: initial 768 MiB, growable to 2 GiB,
  wasm32 headroom is ample.

#### 4. Audio (Phase 3)

- The SoundStream contract is pull-based and thread-friendly: CubebStream's
  `DataCallback` simply calls `m_mixer->Mix(buffer, frames)` on cubeb's own thread
  (AudioCommon/CubebStream.cpp:23-31); the Mixer (fixed 48 kHz output) does all
  resampling internally while emulation pushes into it. `EmscriptenAudioStream :
  SoundStream` reproduces exactly that using Emscripten Audio Worklets
  (`emscripten/webaudio.h` — the worklet runs the wasm module inside
  AudioWorkletGlobalScope on shared memory, so the render callback calls
  `Mixer::Mix` directly; no ring buffer needed unless reentrancy problems appear —
  this direct-mixer approach is the **decided** design; the SAB-ring alternative is
  rejected. AudioContext creation/resume must happen inside the Play-button user
  gesture or Chromium keeps it suspended).
  Register it in `CreateSoundStreamForBackend` (AudioCommon.cpp:28-44). ~10-20 ms
  target latency. DSP-HLE only in v1 (the default; no DSP ROM dumps needed, no DSP
  thread exists under HLE).

#### 5. Input (Phase 3)

- Main thread captures keyboard + Gamepad API state into a SAB block; a ciface backend
  (`ControllerInterface/Emscripten`) exposes them as devices; default Melee-sane
  keyboard profile ships. GC adapter later via a direct WebUSB shim implementing the
  adapter protocol (bypasses libusb — upstream's emscripten backend needs Asyncify,
  which we avoid).

#### 6. Frontend & storage (Phase 0 skeleton, Phase 3 polish)

- `DolphinWeb` embind surface: `init(configJSON)`, `boot(opfsPath)`, `stop()`,
  `pause()/resume()`, `setPadState(port, status)`, `setVolume()`, plus implementations
  of the 24 `Host_*` functions (Core/Host.h:51-85), modeled on
  DolphinNoGUI/MainNoGUI.cpp:56-179.
- Boot sequence copies MainNoGUI.cpp:217-360: `UICommon::SetUserDirectory` →
  `UICommon::Init` → platform init → `UICommon::InitControllers(wsi)` →
  `BootManager::BootCore(system, boot, wsi)` → main loop → orderly shutdown.
  Under `PROXY_TO_PTHREAD` the whole C++ `main` runs on a pthread, so the
  PlatformHeadless-style blocking loop (`while(running){ HostDispatchJobs; sleep }`,
  DolphinNoGUI/PlatformHeadless.cpp:27-35) is legal as-is — no
  `emscripten_set_main_loop` contortions; embind calls from the page proxy into it.
- Sys assets shipped as a fetched+cached bundle into OPFS on first run. GC/Melee
  minimum is small: `Sys/GC` fonts, GameSettings INIs; **no IPL, no DSP ROMs**
  (DSP-HLE is default). Slippi's Gecko codes need no INI — `prepareGeckoList`
  (EXI_DeviceSlippi.cpp:760) builds them at runtime with a built-in `legacy_code_list`
  fallback; extra game files come from `Slippi/GameFiles/GALE01/`
  (SlippiGameFileLoader.cpp:16).
- User dir in OPFS: `Config/` INIs, `GC/` memcards, `Slippi/` replays (D_SLIPPI_IDX;
  .slp written via plain IOFile from the file-write thread — EXI_DeviceSlippi.cpp:378).
- ISO import: File picker → streamed copy into OPFS once; reads via sync access
  handles thereafter. DiscIO's blob layer is plain seek+read (`PlainFileReader`,
  DiscIO/FileBlob.cpp:36-38) — no mmap anywhere outside MemArena, so a WasmFS/OPFS
  mount satisfies it directly.

#### 7. Slippi subsystems in v1 (offline mode)

- `EXI_DeviceSlippi` stays active for LOCAL play (replay recording to OPFS, tags).
- Rust workspace gains a `web` feature: `user` (login), `game-reporter` (uploads),
  `jukebox` (rodio) and anything ureq/`open`-dependent compile to explicit no-ops;
  `dolphin-integrations` tracing routes to `console.log`. ureq/rodio/`open` do not
  build for wasm32-unknown-emscripten — they must be feature-gated out, not ported.
  This also removes the session-lifetime Rust threads (user.json watcher, reporter
  queue + ISO-MD5 hasher, jukebox player) — none are needed offline.
- The spectate server binds an ENet host on port 51441 when its thread starts
  (SlippiSpectate.cpp:173,282) — must be gated off on Emscripten, not just unused.
- Matchmaking/netplay code paths hard-disabled behind the play-type selection (only
  LOCAL offered by the web UI).

**Complete network-touchpoint inventory** (explored; drives the v1 stub list):
ENet — SlippiNetplay (10 peers/3 channels, local port 2626), SlippiMatchmaking
(`mm.slippi.gg:43113`, blocking `gethostbyname` at SlippiMatchmaking.cpp:236),
SlippiSpectate (port 51441); Rust ureq — user auth REST
(`users-rest-dot-slippi.uc.r.appspot.com`), `internal.slippi.gg/graphql`, replay
upload PUTs; C++ curl (`Common::HttpRequest`) — auto-update, cover art (Qt-only),
RetroAchievements, Gecko-code download, mainline-NetPlay index: **all optional for
Slippi gameplay**. Slippi uses no traversal server, no UPnP, no WebSockets (mainline
NetPlay's traversal/UPnP code is dead weight for us). So v1 needs zero network
functionality: enet compiles but is never invoked, curl is stubbed behind
`Common::HttpRequest`, ureq is feature-gated away.

#### 8. Netplay (post-v1, design-ahead only)

Keep one seam: all Slippi peer traffic already flows through ENet in
`SlippiNetplay.cpp`/`SlippiMatchmaking.cpp` (mm connect at SlippiMatchmaking.cpp:346).
The later phase introduces a datagram-transport abstraction with two impls (ENet on
desktop, WebRTC DataChannel unreliable/unordered in browser), a WSS control channel to
matchmaking, and server-side signaling + TURN — a separate spec involving slippi.gg
infrastructure. v1's only obligation: don't add new direct enet call sites.

## Phases and exit criteria

| Phase | Deliverable | Exit criteria |
|---|---|---|
| **0. Toolchain bring-up** | emsdk pin + CMake platform gate; Externals triage; MemArenaEmscripten; Rust stubs (see 0.5); DolphinWeb skeleton; CI job | Headless boot on CachedInterpreter + Null video/audio; per-checkpoint MEM1 XXH64 (direct XXH64 — `Common::GetHash64` is arch-dependent) matches native twin with identical config/stubs, zero panic alerts, clean exit, hard timeouts; CI includes an actual headless-Chromium boot (COOP/COEP served), not just node |
| **0.5 Architecture spikes** (added per 2026-07-10 reviews) | Throwaway prototypes, each with a written result: (1) CPU-worker-local module publication — compile Module on worker A, postMessage, instantiate + table-install on worker B inside a run-loop safe point, `-sALLOW_TABLE_GROWTH`, invalidation generations; **pause-to-resume must measure <1 ms** or the JIT architecture is rejected as designed; (2) WebGPU on a pthread without event-loop deadlock: adapter/device acquisition, uploads via `queue.writeBuffer` (synchronous semantics — mapAsync is only on the readback path), one EFB-style readback via future-wait, OffscreenCanvas transfer to a late-spawned pthread; (3) browser-main→host-thread RPC over Emscripten proxying/HostJobs; (4) Rust `wasm32-unknown-emscripten` staticlib link of the smallest real crate — this target is historically under-maintained, so the spike also validates the fallback: naga built as a separate `wasm32-unknown-unknown` module called over a JS bridge; (5) OPFS/WasmFS random-read of a >1 GiB file | Each spike demonstrably works in headless Chromium, or the affected design section is rewritten before its phase starts. **JitWasm implementation does not begin until spike 1 passes; WebGPU backend not until spike 2 passes** |
| **1. JitWasm** | wasm-emitter lib; JitWasm backend + tiering + compile thread; determinism harness | Melee in-game ≥60 VPS on reference machine (Null video/audio); replay lockstep green vs native |
| **2. WebGPU backend** | `VideoBackends/WebGPU` native-first (wgpu-native, `ENABLE_WGPU`), then emdawnwebgpu in browser; naga-ffi | Melee renders correctly (golden-frame diffs vs Vulkan captures); 60 FPS at 2x IR in browser on reference machine |
| **3. Audio+input+app** | AudioWorklet stream; ciface backend; OPFS import/config UX; pause/resume | "Playable v1": cold page → import ISO → play vs CPU with sound, refresh-safe config/replays |
| **4. Hardening/release** | perf/pacing polish, error surfaces, deploy pipeline + headers, docs | Public beta URL, Chromium floor documented; **full-stack release gate**: JitWasm + WebGPU + audio + input + OPFS together on the reference machine — pinned Chrome version, fixed replay, ≥10 min warm+cold runs, frame-time p95/p99, audio underrun count, memory ceiling |
| **5+. Options** | Replay viewer (v1.5 candidate) · WebRTC netplay (client+server spec) · GC adapter via WebUSB · Jukebox · Firefox/Safari | each gets its own spec/plan |

Reference machine for the perf bar (proposal, confirm before Phase 1 gate): 2021-class
4-core laptop (e.g. i5-1135G7), Chrome stable, integrated graphics.

## Risk register

| # | Risk | Mitigation / gate |
|---|---|---|
| 1 | JitWasm can't hold 60 VPS | Phase 1 hard gate (user-set bar); escalation: megamodules → simd128 → PGO layout; CachedInterpreter fallback always compiled in |
| 2 | Module churn / instantiate latency | batching, background compile tier, persist generated wasm bytes in OPFS keyed by game+build hash (compiled `WebAssembly.Module` objects are not persistable in Chromium — only our bytes are) |
| 3 | wgpu-native ↔ emdawnwebgpu header skew | single `WebGPUCompat.h` shim, pinned versions; Dawn-native fallback for dev loop |
| 4 | EFB peek/poke async-map semantics | Phase 2 spike: future-wait off-main-thread under emdawnwebgpu |
| 5 | Rust deps not wasm-clean (ureq, rodio, `open`) | `web` cargo feature no-ops; corrosion target `wasm32-unknown-emscripten` proven in **Phase 0.5 spike 4** (Phase 0 ships C stubs only; Phase 2 needs real Rust for naga-ffi, so the spike must land first) |
| 6 | Dual-core GPU thread deliberately busy-spins (Fifo.cpp:272; BlockingLoop only sleeps when `m_may_sleep`) — burns a worker core | ship v1 single-core if needed (offline has no rollback pressure); measure in Phase 1; `MAIN_CPU_THREAD=false` is a config flip |
| 6b | wasm32 = ILP32, which the tree has never built for (CMakeLists.txt:226 "this'll break" comment) | `ENABLE_GENERIC`-style arch branch + fix latent pointer-size assumptions as compile errors/harness failures surface |
| 6c | Rust hard-linked into `common` blocks all bring-up until the workspace cross-compiles | Phase 0 ships a C stub shim for the 40 `slprs_*` symbols behind a CMake switch; real `web`-feature crates land before Phase 3 (jukebox/user surfaces needed then) |
| 7 | Frame pacing vs display Hz / rAF jank | audio-clock-driven pacing, decoupled present; measure with Chrome tracing |
| 8 | Emscripten/em sdk churn (C++23, ports) | pin emsdk + emdawnwebgpu versions in-repo; CI enforces |
| 9 | 4 GiB wasm32 ceiling | budget table shows large headroom; wasm64 explicitly deferred |

## Testing strategy

- **Determinism:** replay-driven RAM-hash comparison native↔wasm (DTM in Phase 0, .slp
  playback later); per-instruction lockstep vs Interpreter during JIT bring-up.
- **Graphics:** develop natively under wgpu-native; golden-frame image diffs against
  Vulkan for a fixed capture set; then browser parity screenshots.
- **Unit tests:** existing gtest suites compiled under emscripten run in node for the
  portable subsets (Common, Core math); CI job on PRs.
- **Perf:** scripted headless-Chrome benchmark replay with VPS/frame-time dashboards.

## References

- emdawnwebgpu port & `-sUSE_WEBGPU` removal: emscripten docs "Using WebGPU in
  Emscripten"; emscripten PR #24220; Dawn `src/emdawnwebgpu` README.
- webgpu.h as multi-vendor standard (Dawn/emdawnwebgpu/wgpu-native), wgpu-native header
  lag: webgpu-native/webgpu-headers README.
- Runtime wasm-codegen JIT precedent: v86 architecture notes.
- libusb WebUSB backend (Asyncify-based; motivates the custom shim): web.dev "Porting
  USB applications to the web. Part 1: libusb".

## Open questions (do not block Phase 0)

1. Confirm the reference machine definition for the 60 FPS gate before Phase 1 exit.
2. Hosting target for the beta (slippi.gg subdomain? — deploy pipeline needs COOP/COEP
   header control). Product constraint to keep in view: cross-origin isolation is
   mandatory for SharedArrayBuffer, which rules out third-party embedding (iframes on
   other sites) and constrains CDN setup (every subresource needs CORP/CORS headers).
   Acceptable for a self-hosted v1; any embed story is out of scope.
3. Should the native `ENABLE_WGPU` backend eventually ship to desktop users (extra QA
   surface) or stay a dev tool?
4. Savestate/replay format compatibility guarantees between desktop and web builds
   (proposal: none for v1 beyond .slp correctness).
