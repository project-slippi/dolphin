// Copyright 2026 Dolphin Emulator Project / Slippi
// SPDX-License-Identifier: GPL-2.0-or-later

// Phase-0 Emscripten stub for SlippiRustExtensions. Keeps the slprs_* symbols
// link-resolvable while the Rust workspace is not yet cross-compiled to
// wasm32-unknown-emscripten. Offline (LOCAL) play never exercises these paths
// meaningfully; pointer-returning functions hand back valid empty objects
// because the C++ callers dereference unconditionally (see SlippiUser.cpp).

#include <string.h>

#include "SlippiRustExtensions.h"

// --- EXI device -------------------------------------------------------------

uintptr_t slprs_exi_device_create(struct SlippiRustEXIConfig config)
{
  // Non-zero cookie: callers treat the value as an opaque instance pointer and
  // never dereference it themselves.
  return 1;
}

void slprs_exi_device_destroy(uintptr_t exi_device_instance_ptr)
{
}

void slprs_exi_device_dma_write(uintptr_t exi_device_instance_ptr, const uint8_t* address,
                                const uint8_t* size)
{
}

void slprs_exi_device_dma_read(uintptr_t exi_device_instance_ptr, const uint8_t* address,
                               const uint8_t* size)
{
}

void slprs_exi_device_log_game_report(uintptr_t instance_ptr, uintptr_t game_report_instance_ptr)
{
}

void slprs_exi_device_start_new_reporter_session(uintptr_t instance_ptr)
{
}

void slprs_exi_device_report_match_status(uintptr_t instance_ptr, const char* match_id,
                                          const char* status, bool background)
{
}

void slprs_exi_device_reporter_push_replay_data(uintptr_t instance_ptr, const uint8_t* data,
                                                uint32_t length)
{
}

struct RustIsoMd5Check slprs_get_iso_md5_check(uintptr_t exi_device_instance_ptr)
{
  // status 2 = Complete, result 1 = SafeIso: never block play on a check that
  // will never run.
  struct RustIsoMd5Check check = {2, 1};
  return check;
}

void slprs_exi_device_configure_jukebox(uintptr_t exi_device_instance_ptr, bool is_enabled,
                                        uint8_t initial_dolphin_system_volume,
                                        uint8_t initial_dolphin_music_volume)
{
}

// --- Game reports -----------------------------------------------------------

uintptr_t slprs_player_report_create(const char* uid, uint8_t slot_type, double damage_done,
                                     uint8_t stocks_remaining, uint8_t character_id,
                                     uint8_t color_id, int64_t starting_stocks,
                                     int64_t starting_percent)
{
  return 1;
}

uintptr_t slprs_game_report_create(const char* uid, const char* play_key,
                                   enum SlippiMatchmakingOnlinePlayMode online_mode,
                                   const char* match_id, uint32_t duration_frames,
                                   uint32_t game_index, uint32_t tie_break_index,
                                   int8_t winner_index, uint8_t game_end_method,
                                   int8_t lras_initiator, int32_t stage_id)
{
  return 1;
}

void slprs_game_report_add_player_report(uintptr_t instance_ptr,
                                         uintptr_t player_report_instance_ptr)
{
}

// --- Jukebox ----------------------------------------------------------------

void slprs_jukebox_start_song(uintptr_t exi_device_instance_ptr, uint64_t hps_offset,
                              uintptr_t hps_length)
{
}

void slprs_jukebox_stop_music(uintptr_t exi_device_instance_ptr)
{
}

void slprs_jukebox_set_melee_music_volume(uintptr_t exi_device_instance_ptr, uint8_t volume)
{
}

void slprs_jukebox_set_dolphin_system_volume(uintptr_t exi_device_instance_ptr, uint8_t volume)
{
}

void slprs_jukebox_set_dolphin_music_volume(uintptr_t exi_device_instance_ptr, uint8_t volume)
{
}

// --- Logging ----------------------------------------------------------------

void slprs_logging_init(void (*logger_fn)(int, int, const char*))
{
}

void slprs_logging_register_container(const char* kind, int log_type, bool is_enabled,
                                      int default_log_level)
{
}

void slprs_logging_update_container(const char* kind, bool enabled, int level)
{
}

void slprs_mainline_logging_update_log_level(int level)
{
}

// --- Match results / rank ---------------------------------------------------

void slprs_fetch_match_result(uintptr_t exi_device_instance_ptr, const char* match_id)
{
}

struct RustRankInfo slprs_get_rank_info(uintptr_t exi_device_instance_ptr)
{
  struct RustRankInfo info = {0, 0, 0.0f, 0, 0.0f, 0};
  return info;
}

// --- User -------------------------------------------------------------------

bool slprs_user_attempt_login(uintptr_t exi_device_instance_ptr)
{
  return false;
}

void slprs_user_open_login_page(uintptr_t exi_device_instance_ptr)
{
}

bool slprs_user_update_app(uintptr_t exi_device_instance_ptr)
{
  return false;
}

void slprs_user_listen_for_login(uintptr_t exi_device_instance_ptr)
{
}

void slprs_user_logout(uintptr_t exi_device_instance_ptr)
{
}

void slprs_user_overwrite_latest_version(uintptr_t exi_device_instance_ptr, const char* version)
{
}

bool slprs_user_get_is_logged_in(uintptr_t exi_device_instance_ptr)
{
  return false;
}

struct RustUserInfo* slprs_user_get_info(uintptr_t exi_device_instance_ptr)
{
  // Callers dereference every field and construct std::string from them, so
  // all fields must be valid (empty) C strings. Static: freed by a no-op.
  static struct RustUserInfo s_empty_info = {"", "", "", "", ""};
  return &s_empty_info;
}

void slprs_user_free_info(struct RustUserInfo* ptr)
{
  // s_empty_info is static; nothing to free.
}

struct RustChatMessages* slprs_user_get_messages(uintptr_t exi_device_instance_ptr)
{
  static struct RustChatMessages s_empty_messages = {NULL, 0};
  return &s_empty_messages;
}

struct RustChatMessages* slprs_user_get_default_messages(uintptr_t exi_device_instance_ptr)
{
  static struct RustChatMessages s_empty_messages = {NULL, 0};
  return &s_empty_messages;
}

void slprs_user_free_messages(struct RustChatMessages* ptr)
{
  // Static instances; nothing to free.
}

// --- Direct codes -----------------------------------------------------------

void slprs_user_direct_codes_add_or_update(uintptr_t exi_device_instance_ptr,
                                           enum DirectCodeKind kind, const char* code)
{
}

uint32_t slprs_user_direct_codes_get_length(uintptr_t exi_device_instance_ptr,
                                            enum DirectCodeKind kind)
{
  return 0;
}

char* slprs_user_direct_codes_get_code_at_index(uintptr_t exi_device_instance_ptr,
                                                enum DirectCodeKind kind, uintptr_t index)
{
  // Caller wraps in std::string and passes back to free_code: heap-allocate.
  return strdup("");
}

void slprs_user_direct_codes_free_code(char* code)
{
  free(code);
}
