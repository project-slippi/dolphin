#pragma once

#include "Common/CommonTypes.h"
#include "Common/Thread.h"
#include "Core/Slippi/SlippiNetplay.h"
#include "Core/Slippi/SlippiUser.h"

#include <random>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#ifndef _WIN32
#include <arpa/inet.h>
#include <netdb.h>
#endif

#include <nlohmann/json.hpp>

// Defined in SlippiRustExtensions.h. Forward-declared here because not every project that includes
// this header has the Rust include path.
struct SlippiStunObservation;

using json = nlohmann::json;

// Drives a search for opponents. The conversation with the matchmaking service happens on the Rust
// side; this class owns the UDP socket that will be used for netplay, learns its public address
// through STUN, keeps its NAT mapping alive while queued, and connects to the opponents once a
// match arrives.
class SlippiMatchmaking
{
public:
  SlippiMatchmaking(uintptr_t rs_exi_device_ptr, SlippiUser* user);
  ~SlippiMatchmaking();

  enum OnlinePlayMode
  {
    RANKED = 0,
    UNRANKED = 1,
    DIRECT = 2,
    TEAMS = 3,
    PARTY = 4,
  };

  enum ProcessState
  {
    IDLE,
    INITIALIZING,
    MATCHMAKING,
    OPPONENT_CONNECTING,
    CONNECTION_SUCCESS,
    ERROR_ENCOUNTERED,
  };

  enum SlippiRank
  {
    Unranked,
    Bronze1,
    Bronze2,
    Bronze3,
    Silver1,
    Silver2,
    Silver3,
    Gold1,
    Gold2,
    Gold3,
    Platinum1,
    Platinum2,
    Platinum3,
    Diamond1,
    Diamond2,
    Diamond3,
    Master1,
    Master2,
    Master3,
    Grandmaster
  };

  struct MatchSearchSettings
  {
    OnlinePlayMode mode = OnlinePlayMode::RANKED;
    std::string connect_code = "";
  };

  struct MatchmakeResult
  {
    std::string id = "";
    std::vector<SlippiUser::UserInfo> players;
    std::vector<u16> stages;
    u32 items;
  };

  void FindMatch(MatchSearchSettings settings);
  void MatchmakeThread();
  ProcessState GetMatchmakeState();
  bool IsSearching();
  std::unique_ptr<SlippiNetplayClient> GetNetplayClient();
  std::string GetErrorMessage();
  int LocalPlayerIndex();
  std::vector<SlippiUser::UserInfo> GetPlayerInfo();
  std::string GetPlayerName(u8 port);
  SlippiRank GetPlayerRank(u8 port);
  std::vector<u16> GetStages();
  u8 RemotePlayerCount();
  MatchmakeResult GetMatchmakeResult();
  static bool IsFixedRulesMode(OnlinePlayMode mode);

protected:
  // Public STUN servers used to learn the netplay socket's public address, tried in order until
  // two have answered. Two different servers are needed to tell a cone NAT, which reuses one public
  // port, from a symmetric NAT, which hands out a new one per destination. The non-Google entries
  // are for networks that block Google.
  const std::vector<std::pair<std::string, u16>> STUN_SERVERS = {
      {"stun.l.google.com", 19302},
      {"stun1.l.google.com", 19302},
      {"stun.cloudflare.com", 3478},
      {"global.stun.twilio.com", 3478},
  };

  // How often to send a packet from the netplay socket while queued so the NAT mapping the
  // opponent will use does not expire.
  const u64 STUN_KEEPALIVE_INTERVAL_MS = 15000;

  // Bound to the netplay port for the duration of the queue. Only its socket is used.
  ENetHost* m_client;
  ENetAddress m_stun_addr;
  bool m_has_stun_addr = false;
  u64 m_last_keepalive_ms = 0;

  // Identifies the Rust-side session this object started, so tearing this object down cannot
  // cancel a newer search started by another instance.
  u64 m_session_id = 0;

  std::default_random_engine generator;

  bool is_mm_terminated = false;

  std::thread m_matchmake_thread;

  MatchSearchSettings m_search_settings;

  ProcessState m_state;
  std::string m_error_msg = "";

  SlippiUser* m_user;

  int m_is_swap_attempt = false;

  int m_host_port;
  int m_local_player_idx;
  std::vector<std::string> m_remote_ips;
  MatchmakeResult m_mm_result;
  std::vector<SlippiUser::UserInfo> m_player_info;
  std::vector<u16> m_allowed_stages;
  bool m_joined_lobby;
  bool m_is_host;

  std::unique_ptr<SlippiNetplayClient> m_netplay_client;

  // A pointer to a "shadow" EXI Device that lives on the Rust side of things.
  // Do *not* do any cleanup of this! The EXI device will handle it.
  uintptr_t slprs_exi_device_ptr;

  const std::unordered_map<ProcessState, bool> searching_states = {
      {ProcessState::INITIALIZING, true},
      {ProcessState::MATCHMAKING, true},
      {ProcessState::OPPONENT_CONNECTING, true},
  };

  void terminateMmConnection();

  bool stunBindingRequest(const ENetAddress& server, SlippiStunObservation& result);
  void sendStunKeepalive();

  void startMatchmaking();
  void handleMatchmaking();
  void handleConnecting();
};
