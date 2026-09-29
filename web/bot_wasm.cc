// A candidate's policy and the risk2 engine as a WebAssembly module, for the
// play page's worker (web/static/js/engine_worker.js). Compiled once per
// candidate by web/wasm.bzl, with the submission's header as
// CANDIDATE_ENTRY_HEADER, the way bots/bot.cc is for the referee.
//
// Every export answers web/risk_match.h's JSON, in a buffer that stays valid
// until the next call. Placements cross as "n0,n1,...,n41".

#include <emscripten/emscripten.h>

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

#include "bots/bot_api.h"
#include "web/risk_match.h"

#include CANDIDATE_ENTRY_HEADER

namespace {

std::unique_ptr<risk_web::RiskMatch> g_match;
std::string g_answer;

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

// |params|: the candidate's knobs as the arena passes them, "k=v,k=v".
EMSCRIPTEN_KEEPALIVE const char *rk_new(int human_seat, int max_rounds,
                                        uint32_t seed, const char *params) {
  g_match = std::make_unique<risk_web::RiskMatch>(
      MakePolicy(candidate::Params::Parse(params)), human_seat, max_rounds,
      seed);
  return Answer(g_match->State());
}

EMSCRIPTEN_KEEPALIVE const char *rk_state() { return Answer(g_match->State()); }

EMSCRIPTEN_KEEPALIVE const char *rk_place(int territory) {
  return Answer(g_match->Place(territory));
}

EMSCRIPTEN_KEEPALIVE const char *rk_quick_setup() {
  return Answer(g_match->QuickSetup());
}

EMSCRIPTEN_KEEPALIVE const char *rk_attack(int source, int target,
                                           const char *reinforce, int blitz) {
  return Answer(
      g_match->Attack(source, target, Placement(reinforce), blitz != 0));
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
