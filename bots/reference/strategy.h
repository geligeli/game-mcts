#ifndef GAME_MCTS_BOTS_REFERENCE_STRATEGY_H
#define GAME_MCTS_BOTS_REFERENCE_STRATEGY_H

// The reference bot: the starter every participant's directory is copied
// from (kit.starter_dir), and the stock Risk MCTS strategy. In a kit,
// `arena_cli spar builtin:mcts builtin:random` plays yours locally; in this
// repo:
//
//   bazel run //:match_referee -- --game=risk3 --player_a=reference
//       --player_b=builtin:mcts,builtin:random --games=6 &
//   bazel run //bots/reference:bot --
//       --name=reference --opponent=builtin:mcts,builtin:random --games=6
//
// The whole contract is: include bots/bot_api.h, define MakePolicy. What you
// return is up to you -- the stock MCTS with a proposer of your own (below), a
// different rollout, or a search that is not MCTS at all. Anything callable as
// policy(game, gen) -> mcts::tournament::PolicyDecision<game_t> converts to
// candidate::policy_t.

#include <random>

#include "bots/bot_api.h"

// This example is the stock Risk MCTS bot: the repo's proposer expands the
// tree, and battles in rollouts are resolved by their expected outcome rather
// than rolled out die by die.
inline candidate::policy_t MakePolicy(const candidate::Params &params) {
  using game_t = candidate::game_t;
  using proposer_t = risk_game::RiskProposer<candidate::kNumPlayers>;

  auto rollout = mcts::MakeShortcutRollout<game_t, proposer_t>(
      &risk_game::ResolveBattleWithExpectationInPlace<candidate::kNumPlayers>);

  return mcts::tournament::MctsPolicy<game_t, proposer_t, decltype(rollout)>{
      .iterations_ = params.GetInt("iterations", 400),
      .widening_c_ = params.GetDouble("widening_c", 2.0),
      .widening_alpha_ = params.GetDouble("widening_alpha", 0.5),
      .exploration_c_ = params.GetDouble("exploration_c", 1.0),
      .rollout = rollout,
  };
}

// ---------------------------------------------------------------------------
// Writing your own proposer
// ---------------------------------------------------------------------------
//
// The proposer is where most of the strategy lives: it decides which moves the
// search ever considers. See game_mcts/core/mcts/README.md
// and games/risk/strategies/risk_proposer.h for the full contract. The shape
// is:
//
//   struct MyProposer {
//     using game_t = candidate::game_t;
//     using action_t = game_t::action_t;
//
//     // Every action worth searching, for tree expansion.
//     auto propose(const game_t &state) const -> SomeActionGenerator;
//
//     // One action, for rollouts. Must be fast.
//     auto sample(const game_t &state, std::mt19937 &gen) const -> action_t;
//
//     // Optional (mcts::BoundedProposer): how many actions propose() yields.
//     auto support_size(const game_t &state) const -> std::size_t;
//   };
//   static_assert(mcts::ActionProposer<MyProposer, candidate::game_t>);
//
// Two traps that cost real debugging time, both documented in-tree:
//
//  - support_size() must mirror sample()'s branches exactly, or DedupSampler
//    asserts (game_mcts/core/mcts/game_traits.h).
//  - A proposer that avoids attacking stalls rollouts until the move cap, so
//    games take minutes and finish as draws
//    (game_mcts/games/risk/tuning_result.md). Always sanity-check against
//    builtin:random before submitting.

#endif  // GAME_MCTS_BOTS_REFERENCE_STRATEGY_H
