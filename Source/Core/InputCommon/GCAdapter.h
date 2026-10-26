// Copyright 2014 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <functional>

#include "Common/CommonTypes.h"

struct GCPadStatus;

namespace GCAdapter
{
void Init();
void ResetRumble();
void Shutdown();
void SetAdapterCallback(std::function<void(void)> func);
void StartScanThread();
void StopScanThread();

// slippi change: for poll rate display
bool IsReadingAtReducedRate();
double ReadRate();
// slippi change: for poll rate display

// slippi change: input suggestions
// An input report is a report id byte followed by 9 bytes per port
constexpr int ADAPTER_PAYLOAD_SIZE = 37;

// A report as received from the adapter, along with how many times the game had polled each port
// (see GetGamePollCount) when the report was stored
struct AdapterReport
{
  u64 game_poll_counts[4];
  u8 payload[ADAPTER_PAYLOAD_SIZE];
};

// Called on the adapter read thread for every report received without error. The callback must be
// safe to call at any time from that thread.
using ReportCallback = void (*)(const AdapterReport& report);
void SetReportCallback(ReportCallback callback);
// How many times the game itself has polled a port
u64 GetGamePollCount(int chan);
// slippi change: input suggestions

// Buttons have PAD_GET_ORIGIN set on new connection
// Netplay and CSIDevice_GCAdapter make use of this.
// slippi change: is_game_poll should be set when the game itself is reading the pad, which counts
// towards GetGamePollCount
GCPadStatus Input(int chan, bool is_game_poll = false);

void Output(int chan, u8 rumble_command);
bool IsDetected(const char** error_message);
bool DeviceConnected(int chan);
void ResetDeviceType(int chan);
bool UseAdapter();

}  // namespace GCAdapter
