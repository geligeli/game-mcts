#ifndef GAME_MCTS_BOTS_BOT_API_H
#define GAME_MCTS_BOTS_BOT_API_H

// The contract between a risk2 submission and the harness that runs it.
//
// A submission is one header that includes this file and defines exactly one
// function:
//
//   auto MakePolicy(const candidate::Params &params) -> candidate::policy_t;
//
// Everything else -- connecting, the Play stream, (de)serializing states and
// actions -- is bot.cc, which the arena compiles around the submitted header.
//
// policy_t is mcts::tournament::AnyPolicy, so a submission can return the
// stock MctsPolicy with its own ActionProposer, its own rollout, or a search
// that is not MCTS at all, without any of those types crossing the boundary.

#include <cstdlib>
#include <map>
#include <string>
#include <string_view>
#include <utility>

#include "game_arena/common/kv_options/kv_options.h"
#include "game_mcts/core/mcts/policies.h"
#include "game_mcts/core/mcts/tournament.h"
#include "game_mcts/games/risk/risk_game.h"
#include "game_mcts/games/risk/risk_serialization.h"
#include "game_mcts/games/risk/strategies/risk_proposer.h"
#include "game_mcts/games/risk/strategies/risk_rollout_shortcuts.h"

namespace candidate {

inline constexpr int kNumPlayers = 2;
using game_t = risk_game::RiskState<kNumPlayers>;
inline constexpr std::string_view kGameName = "risk2";

// Any callable of (game, gen) -> PolicyDecision<game_t> converts implicitly.
using policy_t = mcts::tournament::AnyPolicy<game_t>;

// Tuning knobs the arena passes as --params=key=value,key=value. Every getter
// falls back rather than failing, including on a value that does not parse:
// a malformed knob must not take a submission out of the tournament.
class Params {
 public:
  Params() = default;
  explicit Params(std::map<std::string, std::string> values)
      : values_(std::move(values)) {}

  static Params Parse(std::string_view spec) {
    return Params(kv_options::Parse(spec));
  }

  std::string get(std::string_view key, std::string_view fallback) const {
    const auto it = values_.find(std::string(key));
    return it == values_.end() ? std::string(fallback) : it->second;
  }

  int GetInt(std::string_view key, int fallback) const {
    const auto it = values_.find(std::string(key));
    if (it == values_.end()) {
      return fallback;
    }
    char *end = nullptr;
    const long parsed = std::strtol(it->second.c_str(), &end, 10);
    return (end == it->second.c_str() || *end != '\0')
               ? fallback
               : static_cast<int>(parsed);
  }

  double GetDouble(std::string_view key, double fallback) const {
    const auto it = values_.find(std::string(key));
    if (it == values_.end()) {
      return fallback;
    }
    char *end = nullptr;
    const double parsed = std::strtod(it->second.c_str(), &end);
    return (end == it->second.c_str() || *end != '\0') ? fallback : parsed;
  }

  bool contains(std::string_view key) const {
    return values_.contains(std::string(key));
  }

  const std::map<std::string, std::string> &Values() const { return values_; }

 private:
  std::map<std::string, std::string> values_;
};

}  // namespace candidate

// Defined by the submission's own header. Called once per process, before any
// game starts; the returned policy is then used for every game.
candidate::policy_t MakePolicy(const candidate::Params &params);

#endif  // GAME_MCTS_BOTS_BOT_API_H
