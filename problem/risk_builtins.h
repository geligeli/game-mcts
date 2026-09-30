#ifndef GAME_MCTS_PROBLEM_RISK_BUILTINS_H
#define GAME_MCTS_PROBLEM_RISK_BUILTINS_H

// The builtins as policies: random, mcts and mcts_smart. The referee's
// registry (problem/registry.cc) serves them to the arena, and web/ compiles
// them for the play page, so a person meets the very ones a submission is
// rated against. Neither protobuf nor the arena: web/ builds this as
// WebAssembly.

#include <cstddef>
#include <optional>
#include <string_view>

#include "game_mcts/core/mcts/policies.h"
#include "game_mcts/core/mcts/tournament.h"
#include "game_mcts/games/risk/risk_game.h"
#include "game_mcts/games/risk/strategies/risk_proposer.h"
#include "game_mcts/games/risk/strategies/risk_rollout_shortcuts.h"

namespace risk_builtins {

// |name| is "random", "mcts" or "mcts_smart"; nullopt for anything else.
template <size_t N>
std::optional<mcts::tournament::AnyPolicy<risk_game::RiskState<N>>> Make(
    std::string_view name, int iterations) {
  using game_t = risk_game::RiskState<N>;
  using proposer_t = risk_game::RiskProposer<N>;
  // tuning_result.md's strongest: reinforce the borders, attack only at an
  // advantage -- in the tree only; rollouts keep the stock proposer, whose
  // indiscriminate attacks keep playouts short.
  using smart_proposer_t = risk_game::RiskProposer<N, true, true>;
  if (name == "random") {
    return mcts::tournament::ProposerPolicy<game_t, proposer_t>{};
  }
  // Battles in rollouts resolve to their expected outcome, as the reference
  // bot's do.
  auto rollout = mcts::MakeShortcutRollout<game_t, proposer_t>(
      &risk_game::ResolveBattleWithExpectationInPlace<N>);
  if (name == "mcts") {
    return mcts::tournament::MctsPolicy<game_t, proposer_t, decltype(rollout)>{
        .iterations_ = iterations, .rollout = rollout};
  }
  if (name == "mcts_smart") {
    return mcts::tournament::MctsPolicy<game_t, smart_proposer_t,
                                        decltype(rollout)>{
        .iterations_ = iterations, .rollout = rollout};
  }
  return std::nullopt;
}

}  // namespace risk_builtins

#endif  // GAME_MCTS_PROBLEM_RISK_BUILTINS_H
