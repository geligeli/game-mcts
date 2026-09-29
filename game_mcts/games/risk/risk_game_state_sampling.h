#ifndef GAME_MCTS_GAME_MCTS_GAMES_RISK_RISK_GAME_STATE_SAMPLING_H
#define GAME_MCTS_GAME_MCTS_GAMES_RISK_RISK_GAME_STATE_SAMPLING_H
#include <algorithm>
#include <bitset>
#include <cstdint>
#include <optional>
#include <random>
#include <variant>
#include <vector>

#include "absl/log/check.h"
#include "game_mcts/core/util/overloaded.h"
#include "game_mcts/games/risk/risk_board.h"
#include "game_mcts/games/risk/risk_game.h"
#include "game_mcts/games/risk/strategies/expected_battle_outcomes.h"

namespace risk_game {

// Action sets for sampling successor states of a RiskState without
// replacement. Each set is constructed from a state and yields successor
// states via next(): std::nullopt signals exhaustion of the set. This mirrors
// the mcts::ActionGenerator interface, except the draws are successor states
// rather than actions (attack draws resolve the whole battle deterministically
// via the expected-outcome lookup instead of returning a dice action).

template <size_t NUM_PLAYERS>
struct InitialPlaceActionSet {
  explicit InitialPlaceActionSet(const RiskState<NUM_PLAYERS> &state)
      : state_(state) {
    if (state.num_initial_placements_ >= kNumTerritories) {
      // Initial placements after all territories are claimed: players can
      // only place on territories they already own.
      for (size_t i = 0; i < state.map_.size(); ++i) {
        if (state.map_[i].owner == state.current_player_) {
          allowed_territories_.push_back(static_cast<int8_t>(i));
        }
      }
    } else {
      // Initial placement phase: players can only place on unoccupied
      // territories.
      for (size_t i = 0; i < state.map_.size(); ++i) {
        if (state.map_[i].owner == -1) {
          allowed_territories_.push_back(static_cast<int8_t>(i));
        }
      }
    }
  }

  std::optional<RiskState<NUM_PLAYERS>> next(std::mt19937 &) {
    if (allowed_territories_.empty()) {
      return std::nullopt;
    }
    auto action = InitialPlaceAction{.territory_ = allowed_territories_.back()};
    allowed_territories_.pop_back();
    return state_.apply_action(action);
  }

 private:
  std::vector<int8_t> allowed_territories_;
  const RiskState<NUM_PLAYERS> &state_;
};

// Simplified reinforce set: each draw places all reserves on a single owned
// territory (one draw per owned territory). The full space of distributions
// is C(units + territories - 1, territories - 1); see
// mcts::RankedActionSet<mcts::PlaceNElementsIntoKBinsStateSpace> if the full
// space is needed.
template <size_t NUM_PLAYERS>
struct ReinforceActionSet {
  explicit ReinforceActionSet(const RiskState<NUM_PLAYERS> &state)
      : state_(state) {
    for (size_t i = 0; i < state.map_.size(); ++i) {
      if (state.map_[i].owner == state.current_player_) {
        allowed_territories_.push_back(static_cast<int8_t>(i));
      }
    }
    num_units_ = state.reserves_[state.current_player_];
  }

  std::optional<RiskState<NUM_PLAYERS>> next(std::mt19937 &) {
    if (allowed_territories_.empty() || num_units_ <= 0) {
      return std::nullopt;
    }
    ReinforceAction ra;
    ra.units_to_place_.fill(0);
    ra.units_to_place_[allowed_territories_.back()] = num_units_;
    allowed_territories_.pop_back();
    PlayerAction action;
    action.reinforce_action_ = ra;
    return state_.apply_action(action);
  }

 private:
  const RiskState<NUM_PLAYERS> &state_;
  std::vector<int8_t> allowed_territories_;
  int num_units_;
};

// One bit per directed neighbor edge (kAllNeighborEdges contains both
// directions of every border). A set bit is an attack the current player can
// still queue from this set.
template <size_t NUM_PLAYERS>
struct QueueAttackActionSet {
  explicit QueueAttackActionSet(const RiskState<NUM_PLAYERS> &state)
      : state_(state) {
    size_t index = 0;
    for (const auto &edge : kAllNeighborEdges) {
      Country src = edge.first;
      Country tgt = edge.second;
      if (state.map_[static_cast<size_t>(src)].owner == state.current_player_ &&
          state.map_[static_cast<size_t>(tgt)].owner != state.current_player_ &&
          state.map_[static_cast<size_t>(src)].units > 1) {
        source_target_pairs_.set(index);
      }
      ++index;
    }
  }

  bool Any() const { return source_target_pairs_.any(); }

  // Resolves the whole battle deterministically using the expected remnants
  // lookup: the attacker commits all units minus one and fights until the
  // target is captured or the source is down to one unit.
  std::optional<RiskState<NUM_PLAYERS>> next(std::mt19937 &) {
    size_t index = 0;
    for (const auto &edge : kAllNeighborEdges) {
      if (!source_target_pairs_.test(index)) {
        ++index;
        continue;
      }
      source_target_pairs_.set(index, false);
      const size_t src = static_cast<size_t>(edge.first);
      const size_t tgt = static_cast<size_t>(edge.second);

      const int attackers =
          static_cast<int>(state_.map_[src].units) - 1;  // keep one behind
      const int defenders = static_cast<int>(state_.map_[tgt].units);
      BattleRemnants outcome = LookupExpectedRemnants(attackers, defenders);

      auto result = state_;
      // Both expectations are marginals and can both be positive for close
      // battles; the side with more expected survivors wins.
      if (outcome.attackers_ > outcome.defenders_) {
        // Target captured: the survivors move in, and the source keeps them
        // too (the rollout shortcut's overcount, see risk_rollout_shortcuts.h).
        result.map_[src].units -= (attackers - outcome.attackers_);
        result.map_[tgt].units = outcome.attackers_;
        result.map_[tgt].owner = result.current_player_;
      } else {
        // Attack repelled: source down to one unit, defenders remain.
        result.map_[src].units = 1;
        result.map_[tgt].units = outcome.defenders_;
      }
      result.first_attack_of_turn_ = false;
      return result;
    }
    return std::nullopt;
  }

 private:
  const RiskState<NUM_PLAYERS> &state_;
  std::bitset<kAllNeighborEdges.size()> source_target_pairs_;
};

// Defense is deterministic: always defend with the maximum allowed dice.
template <size_t NUM_PLAYERS>
struct QueueDefenseActionSet {
  explicit QueueDefenseActionSet(const RiskState<NUM_PLAYERS> &state)
      : state_(state) {}

  std::optional<RiskState<NUM_PLAYERS>> next(std::mt19937 &) {
    if (consumed_) {
      return std::nullopt;
    }
    consumed_ = true;
    QueueDefenseAction action{
        .num_defend_dice_ =
            std::min(2, static_cast<int>(
                            state_.map_[state_.queued_attack_->target].units))};
    return state_.apply_action(action);
  }

 private:
  const RiskState<NUM_PLAYERS> &state_;
  bool consumed_ = false;
};

// Simplified fortify: one bit per directed edge between two territories of
// the current player where the source has units to spare. Each draw moves all
// units minus one along the edge; a final draw ends the turn without moving
// anything, so the set is never empty. (Proper Risk fortify requires
// connectivity, not just adjacency, and allows moving any number of units.)
template <size_t NUM_PLAYERS>
struct FortifyActionSet {
  explicit FortifyActionSet(const RiskState<NUM_PLAYERS> &state)
      : state_(state) {
    size_t index = 0;
    for (const auto &edge : kAllNeighborEdges) {
      const size_t src = static_cast<size_t>(edge.first);
      const size_t tgt = static_cast<size_t>(edge.second);
      if (state.map_[src].owner == state.current_player_ &&
          state.map_[tgt].owner == state.current_player_ &&
          state.map_[src].units > 1) {
        source_target_pairs_.set(index);
      }
      ++index;
    }
  }

  std::optional<RiskState<NUM_PLAYERS>> next(std::mt19937 &) {
    size_t index = 0;
    for (const auto &edge : kAllNeighborEdges) {
      if (!source_target_pairs_.test(index)) {
        ++index;
        continue;
      }
      source_target_pairs_.set(index, false);
      const size_t src = static_cast<size_t>(edge.first);
      const size_t tgt = static_cast<size_t>(edge.second);
      FortifyAction action{
          .source_ = static_cast<int>(src),
          .target = static_cast<int>(tgt),
          .num_units = static_cast<int>(state_.map_[src].units) - 1};
      return state_.apply_action(action);
    }
    if (!end_turn_drawn_) {
      end_turn_drawn_ = true;
      // num_units <= 1 is the "move nothing, just end the turn" encoding
      // (see RiskState::FortifyToMoveUnits).
      return state_.apply_action(
          FortifyAction{.source_ = 0, .target = 0, .num_units = 0});
    }
    return std::nullopt;
  }

 private:
  const RiskState<NUM_PLAYERS> &state_;
  std::bitset<kAllNeighborEdges.size()> source_target_pairs_;
  bool end_turn_drawn_ = false;
};

// A player turn starts with an optional reinforce (first attack of the turn
// only), followed by any number of queued attacks, and ends with a fortify.
// Draws exhaust the reinforce set first, then the attack set, then the
// fortify set. The fortify set is offered alongside the attacks rather than
// only once no attack is left, so stopping the offensive early is a move the
// search can pick; it is withheld while the reinforce set is still pending,
// because ending the turn there would drop the unplaced reserves.
template <size_t NUM_PLAYERS>
struct PlayerActionSet {
  std::optional<ReinforceActionSet<NUM_PLAYERS>> reinforce_action_;
  std::optional<QueueAttackActionSet<NUM_PLAYERS>> attack_action_;
  std::optional<FortifyActionSet<NUM_PLAYERS>> fortify_action_;

  std::optional<RiskState<NUM_PLAYERS>> next(std::mt19937 &gen) {
    if (reinforce_action_.has_value()) {
      if (auto state = reinforce_action_->next(gen)) {
        return state;
      }
      reinforce_action_.reset();
    }
    if (attack_action_.has_value()) {
      if (auto state = attack_action_->next(gen)) {
        return state;
      }
      attack_action_.reset();
    }
    if (fortify_action_.has_value()) {
      if (auto state = fortify_action_->next(gen)) {
        return state;
      }
      fortify_action_.reset();
    }
    return std::nullopt;
  }
};

// FortifyActionSet is not an alternative here: it is always reached through
// PlayerActionSet, which offers it together with the remaining attacks.
template <size_t NUM_PLAYERS>
using RiskActionSet = std::variant<InitialPlaceActionSet<NUM_PLAYERS>,
                                   PlayerActionSet<NUM_PLAYERS>,
                                   QueueDefenseActionSet<NUM_PLAYERS>>;

template <size_t NUM_PLAYERS>
RiskActionSet<NUM_PLAYERS> ActionSet(const RiskState<NUM_PLAYERS> &state) {
  // Initial placement
  if (state.initial_placement_) {
    return InitialPlaceActionSet<NUM_PLAYERS>(state);
  }

  // Process queued attack
  if (state.queued_attack_.has_value()) {
    if (!state.queued_defense_.has_value()) {
      return QueueDefenseActionSet<NUM_PLAYERS>(state);
    }
    CHECK(false) << "Not enumerating dice actions";
  }

  // Built in place in the variant: moving a locally built PlayerActionSet in
  // trips a -Wmaybe-uninitialized false positive on GCC 13.
  RiskActionSet<NUM_PLAYERS> result{
      std::in_place_type<PlayerActionSet<NUM_PLAYERS>>};
  auto &pa = std::get<PlayerActionSet<NUM_PLAYERS>>(result);
  if (state.first_attack_of_turn_ &&
      state.reserves_[state.current_player_] > 0) {
    pa.reinforce_action_.emplace(state);
  } else {
    // Reserves for this turn are placed, so ending the turn is a legal move
    // from here on, whether or not attacks remain.
    pa.fortify_action_.emplace(state);
  }

  QueueAttackActionSet<NUM_PLAYERS> attacks(state);
  if (attacks.Any()) {
    pa.attack_action_.emplace(std::move(attacks));
  }

  return result;
}

// Draws one successor state from the action set without replacement.
// std::nullopt means the set is exhausted.
template <size_t NUM_PLAYERS>
std::optional<RiskState<NUM_PLAYERS>> SampleSuccessor(
    RiskActionSet<NUM_PLAYERS> &action_set, std::mt19937 &gen) {
  return std::visit([&](auto &set) { return set.next(gen); }, action_set);
}

}  // namespace risk_game

#endif  // GAME_MCTS_GAME_MCTS_GAMES_RISK_RISK_GAME_STATE_SAMPLING_H
