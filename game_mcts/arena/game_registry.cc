#include "game_arena/referee/game_registry.h"

#include <charconv>
#include <map>
#include <string>
#include <string_view>

#include "absl/log/log.h"
#include "game_mcts/arena/benchgame/bench_game.h"
#include "game_mcts/arena/benchgame/bench_serialization.h"
#include "game_mcts/arena/builtins.h"
#include "game_mcts/arena/game_session_impl.h"
#include "game_mcts/core/mcts/mcts.inl"
#include "game_mcts/games/risk/risk_game.h"
#include "game_mcts/games/risk/risk_serialization.h"
#include "game_mcts/games/risk/strategies/risk_proposer.h"
#include "game_mcts/games/risk/strategies/risk_rollout_shortcuts.h"
#include "game_mcts/games/tictactoe/tictactoe.h"
#include "game_mcts/games/tictactoe/tictactoe_serialization.h"

namespace tournament_broker {

namespace {

int g_default_mcts_iterations = 400;

// Parses "mcts" or "mcts:iterations=N"; returns false on a malformed spec.
auto ParseMctsSpec(std::string_view spec, int *iterations,
                   std::string *error) -> bool {
  *iterations = g_default_mcts_iterations;
  if (spec == "mcts") {
    return true;
  }
  constexpr std::string_view kPrefix = "mcts:iterations=";
  if (spec.substr(0, kPrefix.size()) != kPrefix) {
    *error = "unknown builtin spec '" + std::string(spec) + "'";
    return false;
  }
  try {
    *iterations = std::stoi(std::string(spec.substr(kPrefix.size())));
  } catch (const std::exception &) {
    *error = "bad iterations in builtin spec '" + std::string(spec) + "'";
    return false;
  }
  if (*iterations <= 0) {
    *error = "iterations must be positive in '" + std::string(spec) + "'";
    return false;
  }
  return true;
}

// ---------------------------------------------------------------------------
// Risk (2 players)
// ---------------------------------------------------------------------------

using risk_game_t = risk_game::RiskState<2>;
using risk_proposer_t = risk_game::RiskProposer<2>;

// MCTS with the shortcut (expected-battle) rollout, mirroring RiskMctsPolicy
// in games/risk/risk_tournament.cpp.
auto RiskMctsBuiltin(int iterations) -> BuiltinFn {
  return [iterations](std::string_view state_bytes,
                      std::mt19937 &gen) -> std::string {
    using traits = mcts::GameSerializationTraits<risk_game_t>;
    traits::state_proto_t state_proto;
    state_proto.ParseFromArray(state_bytes.data(),
                               static_cast<int>(state_bytes.size()));
    const risk_game_t state = traits::StateFromProto(state_proto);

    const risk_proposer_t proposer{};
    auto rollout = mcts::MakeShortcutRollout<risk_game_t, risk_proposer_t>(
        &risk_game::ResolveBattleWithExpectationInPlace<2>);
    mcts::MctsRunner<risk_game_t, risk_proposer_t, decltype(rollout)> runner(
        state, proposer, rollout);
    auto picker =
        mcts::MctsStochasticNodePicker<risk_game_t>(gen, 2.0, 0.5, 1.0);
    for (int i = 0; i < iterations; ++i) {
      runner.OneIteration(picker, gen);
    }
    return traits::ActionToProto(runner.best_action()).SerializeAsString();
  };
}

auto MakeRiskBuiltin(std::string_view spec,
                     std::string *error) -> std::optional<BuiltinFn> {
  if (spec == "random") {
    return RandomBuiltin<risk_game_t>(risk_proposer_t{});
  }
  int iterations;
  if (ParseMctsSpec(spec, &iterations, error)) {
    return RiskMctsBuiltin(iterations);
  }
  if (error->empty()) {
    *error = "unknown builtin spec '" + std::string(spec) + "'";
  }
  return std::nullopt;
}

// ---------------------------------------------------------------------------
// TicTacToe
// ---------------------------------------------------------------------------

using tictactoe::TicTacToe;

auto MakeTicTacToeBuiltin(std::string_view spec,
                          std::string *error) -> std::optional<BuiltinFn> {
  if (spec == "random") {
    return RandomBuiltin<TicTacToe>(mcts::DefaultProposer<TicTacToe>{});
  }
  if (spec == "minimax") {
    return MinimaxBuiltin<TicTacToe>();
  }
  *error = "unknown builtin spec '" + std::string(spec) + "'";
  return std::nullopt;
}

// ---------------------------------------------------------------------------
// Bench -- load-test filler, not a real game
// ---------------------------------------------------------------------------

using benchgame::BenchGame;

auto MakeBenchBuiltin(std::string_view spec,
                      std::string *error) -> std::optional<BuiltinFn> {
  if (spec == "random") {
    return RandomBuiltin<BenchGame>(mcts::DefaultProposer<BenchGame>{});
  }
  *error = "unknown builtin spec '" + std::string(spec) + "'";
  return std::nullopt;
}

}  // namespace

void SetRegistryOptions(const std::map<std::string, std::string> &options) {
  // The arena hands these over untouched; reading the keys we understand and
  // ignoring the rest is the contract. An unparsable or absent value leaves
  // the compiled-in default, because a bad option is not worth failing an
  // order that would otherwise run.
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

auto GameRegistry() -> const std::map<std::string, GameDescriptor> & {
  static const auto *kRegistry = new std::map<std::string, GameDescriptor>{
      {"risk2",
       GameDescriptor{
           .name = "risk2",
           .new_session =
               [] { return std::make_unique<GameSessionImpl<risk_game_t>>(); },
           .make_builtin = MakeRiskBuiltin,
       }},
      {"bench",
       GameDescriptor{
           .name = "bench",
           .new_session =
               [] { return std::make_unique<GameSessionImpl<BenchGame>>(); },
           .make_builtin = MakeBenchBuiltin,
       }},
      {"tictactoe",
       GameDescriptor{
           .name = "tictactoe",
           .new_session =
               [] { return std::make_unique<GameSessionImpl<TicTacToe>>(); },
           .make_builtin = MakeTicTacToeBuiltin,
       }},
  };
  return *kRegistry;
}

}  // namespace tournament_broker
