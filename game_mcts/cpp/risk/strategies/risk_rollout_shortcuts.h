#ifndef RISK_GAME_AI_CPP_RISK_STRATEGIES_RISK_ROLLOUT_SHORTCUTS_H
#define RISK_GAME_AI_CPP_RISK_ROLLOUT_SHORTCUTS_H

// Move shortcuts for MCTS rollouts on RiskState (see "Pluggable rollout
// policies" in the game-mcts repo's mcts.h). These are approximations used
// only during MCTS rollouts; the exact game logic stays in risk_game.h.

#include <optional>
#include <random>

#include "cpp/risk/risk_game.h"
#include "cpp/risk/strategies/expected_battle_outcomes.h"

namespace risk_game {

// In-place variant of ResolveBattleWithExpectation: mutates |state| and
// returns true when a fully queued battle was resolved; returns false for all
// other states, where the rollout falls back to a proposer-driven
// mcts::PlayoutStep. ShortcutRollout prefers this form when the passed
// shortcut has this signature (see mcts::MoveShortcutInPlace).
template <size_t NUM_PLAYERS>
auto ResolveBattleWithExpectationInPlace(RiskState<NUM_PLAYERS> &state,
                                         std::mt19937 & /*gen*/) -> bool {
  if (!state.queued_attack.has_value() || !state.queued_defense.has_value()) {
    return false;
  }
  Territory &src = state.m_map[state.queued_attack->source];
  Territory &tgt = state.m_map[state.queued_attack->target];
  const int attackers = static_cast<int>(src.units) - 1;  // keep one behind
  const int defenders = static_cast<int>(tgt.units);
  const BattleRemnants outcome = LookupExpectedRemnants(attackers, defenders);
  // Both expectations are marginals and can both be positive for close
  // battles; the side with more expected survivors wins.
  if (outcome.attackers > outcome.defenders) {
    // Target captured: surviving attackers move in, losses stay behind.
    src.units -= (attackers - outcome.attackers);
    tgt.units = outcome.attackers;
    tgt.owner = src.owner;
  } else {
    // Attack repelled: source down to one unit, defenders remain.
    src.units = 1;
    tgt.units = outcome.defenders;
  }
  state.m_current_player = src.owner;
  state.queued_attack = std::nullopt;
  state.queued_defense = std::nullopt;
  return true;
}

// Resolves a fully queued battle in one step with the expected-remnants table
// (fight until the target is captured or the source is down to one unit)
// instead of rolling dice round by round. Returns nullopt for all other
// states, where the rollout falls back to a proposer-driven
// mcts::PlayoutStep.
template <size_t NUM_PLAYERS>
auto ResolveBattleWithExpectation(const RiskState<NUM_PLAYERS> &state,
                                  std::mt19937 &gen)
    -> std::optional<RiskState<NUM_PLAYERS>> {
  RiskState<NUM_PLAYERS> next = state;
  if (!ResolveBattleWithExpectationInPlace(next, gen)) {
    return std::nullopt;
  }
  return next;
}

}  // namespace risk_game

#endif  // RISK_GAME_AI_CPP_RISK_STRATEGIES_RISK_ROLLOUT_SHORTCUTS_H
