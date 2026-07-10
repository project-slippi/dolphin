// Copyright 2026 Dolphin Emulator Project / Slippi
// SPDX-License-Identifier: GPL-2.0-or-later

// Host_* implementations for the browser frontend. Modeled on
// DolphinNoGUI/MainNoGUI.cpp with the Platform object replaced by
// file-local state (there is no window system in Phase 0).

#include "DolphinWeb/HostWeb.h"

#include <memory>
#include <string>
#include <vector>

#include "Common/Event.h"
#include "Common/Flag.h"
#include "Core/Host.h"

namespace DolphinWeb
{
static Common::Flag s_running{true};

bool IsRunning()
{
  return s_running.IsSet();
}

void RequestStop()
{
  s_running.Clear();
}
}  // namespace DolphinWeb

std::vector<std::string> Host_GetPreferredLocales()
{
  return {};
}

void Host_PPCSymbolsChanged()
{
}

void Host_PPCBreakpointsChanged()
{
}

void Host_RefreshDSPDebuggerWindow()
{
}

bool Host_UIBlocksControllerState()
{
  return false;
}

void Host_Message(HostMessageID id)
{
  if (id == HostMessageID::WMUserStop)
    DolphinWeb::RequestStop();
}

void Host_UpdateTitle(const std::string& title)
{
}

void Host_UpdateDisasmDialog()
{
}

void Host_JitCacheInvalidation()
{
}

void Host_JitProfileDataWiped()
{
}

void Host_UpdateMainFrame()
{
}

void Host_RequestRenderWindowSize(int width, int height)
{
}

bool Host_RendererHasFocus()
{
  return true;
}

bool Host_RendererHasFullFocus()
{
  return true;
}

bool Host_RendererIsFullscreen()
{
  return false;
}

bool Host_TASInputHasFocus()
{
  return false;
}

void Host_YieldToUI()
{
}

void Host_TitleChanged()
{
}

void Host_LowerWindow()
{
}

void Host_Exit()
{
  DolphinWeb::RequestStop();
}

void Host_PlaybackSeek()
{
}

void Host_Fullscreen()
{
}

void Host_UpdateDiscordClientID(const std::string& client_id)
{
}

bool Host_UpdateDiscordPresenceRaw(const std::string& details, const std::string& state,
                                   const std::string& large_image_key,
                                   const std::string& large_image_text,
                                   const std::string& small_image_key,
                                   const std::string& small_image_text,
                                   const int64_t start_timestamp, const int64_t end_timestamp,
                                   const int party_size, const int party_max)
{
  return false;
}

std::unique_ptr<GBAHostInterface> Host_CreateGBAHost(std::weak_ptr<HW::GBA::Core> core)
{
  return nullptr;
}
