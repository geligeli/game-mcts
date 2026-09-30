// The GameRegistry() the arena declares and the referee links: risk2 and
// risk3, Risk for two and for three, and their builtins, random, mcts and
// mcts_smart.

#include "game_arena/referee/game_registry.h"

#include <charconv>
#include <cstddef>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>

#include "absl/log/log.h"
#include "absl/strings/strip.h"
#include "game_mcts/core/mcts/policies.h"
#include "game_mcts/games/risk/risk_game.h"
#include "game_mcts/games/risk/risk_serialization.h"
#include "problem/risk_builtins.h"
#include "problem/risk_session.h"

namespace tournament_broker {

namespace {

int g_default_mcts_iterations = 400;
int g_max_rounds = 0;  // no cap unless the problem sets one

bool ParseNonNegative(std::string_view text, int *value) {
  const auto parsed =
      std::from_chars(text.data(), text.data() + text.size(), *value);
  return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size() &&
         *value >= 0;
}

// "random", or "mcts" / "mcts_smart", each optionally ":iterations=N".
template <size_t N>
std::optional<BuiltinFn> MakeRiskBuiltin(std::string_view spec,
                                         std::string *error) {
  const std::string_view name = spec.substr(0, spec.find(':'));
  int iterations = g_default_mcts_iterations;
  // Only a search takes a knob.
  if (name.size() < spec.size()) {
    std::string_view rest = spec.substr(name.size() + 1);
    if (name == "random" || !absl::ConsumePrefix(&rest, "iterations=") ||
        !ParseNonNegative(rest, &iterations) || iterations == 0) {
      *error = "want " + std::string(name) + ":iterations=N, N > 0, not '" +
               std::string(spec) + "'";
      return std::nullopt;
    }
  }
  auto policy = risk_builtins::Make<N>(name, iterations);
  if (!policy.has_value()) {
    *error = "unknown builtin spec '" + std::string(spec) +
             "': random, mcts or mcts_smart";
    return std::nullopt;
  }
  return mcts::tournament::SerializedPolicy<risk_game::RiskState<N>>(
      std::move(*policy));
}

// A forfeiter's seat is played on by the strongest builtin, so its armies stay
// an obstacle rather than a gift to whoever sits next to them.
template <size_t N>
GameDescriptor RiskDescriptor() {
  return GameDescriptor{
      .name = "risk" + std::to_string(N),
      .new_session =
          [] { return std::make_unique<RiskSession<N>>(g_max_rounds); },
      .make_builtin = MakeRiskBuiltin<N>,
      .num_players = static_cast<int>(N),
      .forfeit_builtin = "mcts_smart",
  };
}

}  // namespace

void SetRegistryOptions(const std::map<std::string, std::string> &options) {
  // Unknown keys are ignored, and a bad value keeps the default: neither is
  // worth failing an order that would otherwise run.
  for (const auto &[key, target, positive] :
       {std::tuple{"mcts_iterations", &g_default_mcts_iterations, true},
        std::tuple{"max_rounds", &g_max_rounds, false}}) {
    const auto it = options.find(key);
    if (it == options.end()) {
      continue;
    }
    int value = 0;
    if (ParseNonNegative(it->second, &value) && (!positive || value > 0)) {
      *target = value;
    } else {
      LOG(WARNING) << "ignoring registry option " << key << "='" << it->second
                   << "'";
    }
  }
}

const std::map<std::string, GameDescriptor> &GameRegistry() {
  static const auto *registry = new std::map<std::string, GameDescriptor>{
      {"risk2", RiskDescriptor<2>()},
      {"risk3", RiskDescriptor<3>()},
  };
  return *registry;
}

}  // namespace tournament_broker
