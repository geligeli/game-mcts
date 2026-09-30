// A candidate's policy and the Risk engine as a WebAssembly module, for the
// play page's worker (web/static/js/engine_worker.js). Compiled once per
// candidate by web/wasm.bzl, with the submission's header as
// CANDIDATE_ENTRY_HEADER, the way bots/bot.cc is for the referee.
//
// Every export answers web/risk_match.h's JSON, in a buffer that stays valid
// until the next call. Placements cross as "n0,n1,...,n41".

#include <emscripten/emscripten.h>

#include <array>
#include <bit>
#include <cstdint>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include "bots/bot_api.h"
#include "web/risk_match.h"

#include CANDIDATE_ENTRY_HEADER

namespace {

std::unique_ptr<risk_web::RiskMatch> g_match;
// This candidate as the outside seat of another module's match (rk_policy).
std::optional<risk_web::policy_t> g_policy;
std::mt19937 g_gen;
std::string g_answer;

// A state or an action crosses to another module as its bytes: both are
// compiled from the same engine, so they agree on the layout.
static_assert(std::is_trivially_copyable_v<risk_web::state_t>);
static_assert(std::is_trivially_copyable_v<risk_game::RiskAction>);

template <typename T>
std::string Hex(const T &value) {
  constexpr char kDigits[] = "0123456789abcdef";
  std::string hex;
  for (const unsigned char byte :
       std::bit_cast<std::array<unsigned char, sizeof(T)>>(value)) {
    hex += kDigits[byte >> 4];
    hex += kDigits[byte & 0xf];
  }
  return "{\"bytes\":\"" + hex + "\"}";
}

template <typename T>
T FromHex(std::string_view hex) {
  const auto nibble = [](char c) { return c <= '9' ? c - '0' : c - 'a' + 10; };
  std::array<unsigned char, sizeof(T)> bytes{};
  for (std::size_t i = 0; i < bytes.size() && 2 * i + 1 < hex.size(); ++i) {
    bytes[i] = static_cast<unsigned char>(nibble(hex[2 * i]) << 4 |
                                          nibble(hex[2 * i + 1]));
  }
  return std::bit_cast<T>(bytes);
}

const char *Answer(std::string json) {
  g_answer = std::move(json);
  return g_answer.c_str();
}

risk_web::placement_t Placement(std::string_view text) {
  risk_web::placement_t placement{};
  std::size_t t = 0;
  for (const char c : text) {
    if (c == ',') {
      ++t;
    } else if (c >= '0' && c <= '9' && t < placement.size()) {
      placement[t] = static_cast<uint16_t>(placement[t] * 10 + (c - '0'));
    }
  }
  return placement;
}

}  // namespace

extern "C" {

// |params|: the candidate's knobs as the arena passes them, "k=v,k=v". The
// candidate plays every seat but the person's and |outside_seat| (-1: none),
// a policy of its own in each.
EMSCRIPTEN_KEEPALIVE const char *rk_new(int human_seat, int max_rounds,
                                        uint32_t seed, const char *params,
                                        int outside_seat) {
  std::vector<risk_web::policy_t> bots;
  for (size_t seat = 1; seat < risk_web::kPlayers; ++seat) {
    bots.push_back(MakePolicy(candidate::Params::Parse(params)));
  }
  g_match = std::make_unique<risk_web::RiskMatch>(
      std::move(bots), human_seat, max_rounds, seed, outside_seat);
  return Answer(g_match->State());
}

// The match as it stands, for the outside seat's module to decide on.
EMSCRIPTEN_KEEPALIVE const char *rk_snapshot() {
  return Answer(Hex(g_match->state()));
}

// Makes this candidate the outside seat of another module's match.
EMSCRIPTEN_KEEPALIVE const char *rk_policy(uint32_t seed, const char *params) {
  g_policy = MakePolicy(candidate::Params::Parse(params));
  g_gen.seed(seed);
  return Answer("{}");
}

// Its decision on another module's rk_snapshot().
EMSCRIPTEN_KEEPALIVE const char *rk_decide(const char *snapshot) {
  return Answer(
      Hex((*g_policy)(FromHex<risk_web::state_t>(snapshot), g_gen).action));
}

// The outside seat's decision, from its module's rk_decide().
EMSCRIPTEN_KEEPALIVE const char *rk_act(const char *action) {
  return Answer(g_match->Act(FromHex<risk_game::RiskAction>(action)));
}

EMSCRIPTEN_KEEPALIVE const char *rk_state() { return Answer(g_match->State()); }

EMSCRIPTEN_KEEPALIVE const char *rk_place(int territory) {
  return Answer(g_match->Place(territory));
}

EMSCRIPTEN_KEEPALIVE const char *rk_quick_setup() {
  return Answer(g_match->QuickSetup());
}

EMSCRIPTEN_KEEPALIVE const char *rk_attack(int source, int target,
                                           const char *reinforce, int blitz,
                                           int move) {
  return Answer(
      g_match->Attack(source, target, Placement(reinforce), blitz != 0, move));
}

EMSCRIPTEN_KEEPALIVE const char *rk_fortify(int source, int target, int units,
                                            const char *reinforce) {
  return Answer(g_match->Fortify(source, target, units, Placement(reinforce)));
}

EMSCRIPTEN_KEEPALIVE const char *rk_bot_step() {
  return Answer(g_match->BotStep());
}

EMSCRIPTEN_KEEPALIVE void rk_fast_defense(int fast) {
  g_match->set_fast_defense(fast != 0);
}

EMSCRIPTEN_KEEPALIVE const char *rk_board() {
  return Answer(risk_web::BoardJson());
}

}  // extern "C"
