// Copyright 2026 Dolphin Emulator Project / Slippi
// SPDX-License-Identifier: GPL-2.0-or-later

// Phase-0 headless browser entry point: boot a game from the Emscripten FS,
// frame-step N frames deterministically, hash MEM1, exit. Runs identically as
// a native binary (dolphin-headless-smoke) so native and wasm runs can be
// diffed. Grows into the embind frontend in Phase 3.

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>

#include "Common/CommonTypes.h"
#include "Common/Config/Config.h"
#include "Common/Hash.h"
#include "Common/Logging/LogManager.h"
#include "Common/MsgHandler.h"
#include "Common/WindowSystemInfo.h"
#include "Core/Boot/Boot.h"
#include "Core/BootManager.h"
#include "Core/Config/MainSettings.h"
#include "Core/Core.h"
#include "Core/HW/SI/SI_Device.h"
#include "Core/HW/Memmap.h"
#include "Core/PowerPC/PowerPC.h"
#include "Core/System.h"
#include "DolphinWeb/HostWeb.h"
#include "UICommon/UICommon.h"

namespace
{
bool WaitForState(Core::System& system, Core::State state, int timeout_ms)
{
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
  while (Core::GetState(system) != state)
  {
    if (!DolphinWeb::IsRunning() || std::chrono::steady_clock::now() > deadline)
      return false;
    Core::HostDispatchJobs(system);
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  return true;
}
}  // namespace

int main(int argc, char* argv[])
{
  std::string path;
#ifdef __EMSCRIPTEN__
  std::string user_dir = "/User";  // MEMFS
#else
  std::string user_dir;  // must be passed for isolated native runs
#endif
  int frames = 600;
  for (int i = 1; i < argc; i++)
  {
    if (std::strcmp(argv[i], "--exec") == 0 && i + 1 < argc)
      path = argv[++i];
    else if (std::strcmp(argv[i], "--frames") == 0 && i + 1 < argc)
      frames = std::atoi(argv[++i]);
    else if (std::strcmp(argv[i], "--user") == 0 && i + 1 < argc)
      user_dir = argv[++i];
  }
  if (path.empty())
  {
    std::fprintf(stderr, "usage: dolphin-web --exec <path> [--frames N]\n");
    return 1;
  }

  Core::DeclareAsHostThread();

  UICommon::SetUserDirectory(user_dir);
  UICommon::Init();

  if (auto* log_manager = Common::Log::LogManager::GetInstance())
  {
    log_manager->SetLogLevel(Common::Log::LogLevel::LWARNING);
    log_manager->EnableListener(Common::Log::LogListener::CONSOLE_LISTENER, true);
    for (int i = 0; i < static_cast<int>(Common::Log::LogType::NUMBER_OF_LOGS); i++)
      log_manager->SetEnable(static_cast<Common::Log::LogType>(i), true);
  }
  Common::SetEnableAlert(true);
  Common::RegisterMsgAlertHandler([](const char* caption, const char* text, bool yes_no,
                                     Common::MsgType style) {
    std::fprintf(stderr, "ALERT [%s]: %s\n", caption, text);
    return true;  // headless: auto-confirm
  });

  // Deterministic headless configuration, identical for native and wasm runs.
  Config::SetBaseOrCurrent(Config::MAIN_GFX_BACKEND, std::string("Null"));
  Config::SetBaseOrCurrent(Config::MAIN_AUDIO_BACKEND, std::string("No Audio Output"));
  Config::SetBaseOrCurrent(Config::MAIN_CPU_CORE, PowerPC::CPUCore::CachedInterpreter);
  Config::SetBaseOrCurrent(Config::MAIN_CPU_THREAD, false);
  Config::SetBaseOrCurrent(Config::MAIN_FASTMEM, false);
  Config::SetBaseOrCurrent(Config::MAIN_EMULATION_SPEED, 0.0f);
  // Fixed RTC (2000-01-01): the emulated clock otherwise leaks host time into
  // guest memory and breaks run-to-run hash comparison.
  Config::SetBaseOrCurrent(Config::MAIN_CUSTOM_RTC_ENABLE, true);
  Config::SetBaseOrCurrent(Config::MAIN_CUSTOM_RTC_VALUE, u32{946684800});
  // No controllers: host input devices (present natively, absent in wasm)
  // must not feed divergent pad data into the comparison.
  for (int port = 0; port < 4; port++)
  {
    Config::SetBaseOrCurrent(Config::GetInfoForSIDevice(port),
                             SerialInterface::SIDEVICE_NONE);
  }

  WindowSystemInfo wsi;
  wsi.type = WindowSystemType::Headless;

  UICommon::InitControllers(wsi);

  auto& system = Core::System::GetInstance();

  // Deterministic stopping point: at VI field `frames`, hash MEM1 on the CPU
  // thread (memory is quiescent there) and request shutdown. Frame-stepping
  // can't be used: it only completes on presentation, which the Null backend
  // never does.
  static std::atomic<bool> s_hash_done{false};
  static std::atomic<u64> s_mem1_hash{0};
  const u64 target_fields = static_cast<u64>(frames);
  Core::SetOnFieldCallback([target_fields](Core::System& sys, u64 field_count) {
    if (field_count == target_fields)
    {
      auto& mem = sys.GetMemory();
      s_mem1_hash.store(Common::GetHash64(mem.GetRAM(), mem.GetRamSizeReal(), 0));
      s_hash_done.store(true);
    }
  });

  auto boot = BootParameters::GenerateFromFile(path);
  if (!boot)
  {
    std::fprintf(stderr, "SLIPPI_WEB_BOOT_FAILED (no boot parameters for %s)\n", path.c_str());
    return 1;
  }
  if (!BootManager::BootCore(system, std::move(boot), wsi))
  {
    std::fprintf(stderr, "SLIPPI_WEB_BOOT_FAILED (BootCore)\n");
    return 1;
  }

  if (!WaitForState(system, Core::State::Running, 60000))
  {
    std::fprintf(stderr, "SLIPPI_WEB_BOOT_TIMEOUT\n");
    return 1;
  }
  std::printf("SLIPPI_WEB_BOOTED\n");
  std::fflush(stdout);

  // Wait for the field callback to capture the hash (unthrottled emulation).
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(300);
  while (!s_hash_done.load() && DolphinWeb::IsRunning())
  {
    if (std::chrono::steady_clock::now() > deadline)
    {
      std::fprintf(stderr, "SLIPPI_WEB_RUN_TIMEOUT\n");
      return 1;
    }
    Core::HostDispatchJobs(system);
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  Core::SetOnFieldCallback(nullptr);

  std::printf("SLIPPI_WEB_MEM1_HASH=%016llx\n",
              static_cast<unsigned long long>(s_mem1_hash.load()));
  std::fflush(stdout);

  Core::Stop(system);
  Core::Shutdown(system);
  UICommon::ShutdownControllers();
  UICommon::Shutdown();
  return 0;
}
