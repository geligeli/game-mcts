// The GameRegistry() the arena declares and the referee links: risk2 and its
// builtins.

#include "game_arena/referee/game_registry.h"

#include <charconv>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "absl/log/log.h"
#include "game_mcts/core/mcts/policies.h"
#include "game_mcts/games/risk/risk_game.h"
#include "game_mcts/games/risk/risk_serialization.h"
#include "game_mcts/games/risk/strategies/risk_proposer.h"
#include "game_mcts/games/risk/strategies/risk_rollout_shortcuts.h"
#include "problem/session.h"

namespace tournament_broker {

namespace {

using risk_game_t = risk_game::RiskState<2>;
using risk_proposer_t = risk_game::RiskProposer<2>;

int g_default_mcts_iterations = 400;

// Parses "mcts" or "mcts:iterations=N"; returns false on a malformed spec.
bool ParseMctsSpec(std::string_view spec, int *iterations, std::string *error) {
  *iterations = g_default_mcts_iterations;
  if (spec == "mcts") {
    return true;
  }
  constexpr std::string_view prefix = "mcts:iterations=";
  if (!spec.starts_with(prefix)) {
    *error = "unknown builtin spec '" + std::string(spec) + "'";
    return false;
  }
  const std::string_view value = spec.substr(prefix.size());
  const auto parsed =
      std::from_chars(value.data(), value.data() + value.size(), *iterations);
  if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() ||
      *iterations <= 0) {
    *error = "want a positive iteration count in '" + std::string(spec) + "'";
    return false;
  }
  return true;
}

std::optional<BuiltinFn> MakeRiskBuiltin(std::string_view spec,
                                         std::string *error) {
  using mcts::tournament::SerializedPolicy;
  if (spec == "random") {
    return SerializedPolicy<risk_game_t>(
        mcts::tournament::ProposerPolicy<risk_game_t, risk_proposer_t>{});
  }
  int iterations = 0;
  if (!ParseMctsSpec(spec, &iterations, error)) {
    return std::nullopt;
  }
  // Battles in rollouts resolve to their expected outcome, as the reference
  // bot's do.
  auto rollout = mcts::MakeShortcutRollout<risk_game_t, risk_proposer_t>(
      &risk_game::ResolveBattleWithExpectationInPlace<2>);
  return SerializedPolicy<risk_game_t>(
      mcts::tournament::MctsPolicy<risk_game_t, risk_proposer_t,
                                   decltype(rollout)>{.iterations_ = iterations,
                                                      .rollout = rollout});
}

}  // namespace

void SetRegistryOptions(const std::map<std::string, std::string> &options) {
  // Unknown keys are ignored, and a bad value keeps the default: neither is
  // worth failing an order that would otherwise run.
  const auto it = options.find("mcts_iterations");
  if (it == options.end()) {
    return;
  }
  int iterations = 0;
  const char *begin = it->second.data();
  const char *end = begin + it->second.size();
  const std::from_chars_result parsed = std::from_chars(begin, end, iterations);
  if (parsed.ec == std::errc{} && parsed.ptr == end && iterations > 0) {
    g_default_mcts_iterations = iterations;
  } else {
    LOG(WARNING) << "ignoring registry option mcts_iterations='" << it->second
                 << "': want a positive integer";
  }
}

const std::map<std::string, GameDescriptor> &GameRegistry() {
  static const auto *registry = new std::map<std::string, GameDescriptor>{
      {"risk2",
       GameDescriptor{
           .name = "risk2",
           .new_session =
               [] { return std::make_unique<GameSessionImpl<risk_game_t>>(); },
           .make_builtin = MakeRiskBuiltin,
       }},
  };
  return *registry;
}

}  // namespace tournament_broker
