#ifndef RISK_GAME_AI_CPP_RISK_RISK_GAME_STATE_SAMPLING_H
#define RISK_GAME_AI_CPP_RISK_RISK_GAME_STATE_SAMPLING_H
#include <algorithm>
#include <bitset>
#include <cstdint>
#include <optional>
#include <random>
#include <variant>
#include <vector>

#include "absl/log/check.h"
#include "cpp/risk/risk_board.h"
#include "cpp/risk/risk_game.h"
#include "cpp/risk/strategies/expected_battle_outcomes.h"
#include "game_mcts/cpp/mcts/overloaded.h"

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
      : m_state(state) {
    if (state.m_num_initial_placements >= kNumTerritories) {
      // Initial placements after all territories are claimed: players can
      // only place on territories they already own.
      for (size_t i = 0; i < state.m_map.size(); ++i) {
        if (state.m_map[i].owner == state.m_current_player) {
          m_allowed_territories.push_back(static_cast<int8_t>(i));
        }
      }
    } else {
      // Initial placement phase: players can only place on unoccupied
      // territories.
      for (size_t i = 0; i < state.m_map.size(); ++i) {
        if (state.m_map[i].owner == -1) {
          m_allowed_territories.push_back(static_cast<int8_t>(i));
        }
      }
    }
  }

  auto next(std::mt19937 &) -> std::optional<RiskState<NUM_PLAYERS>> {
    if (m_allowed_territories.empty()) {
      return std::nullopt;
    }
    auto action = InitialPlaceAction{.territory = m_allowed_territories.back()};
    m_allowed_territories.pop_back();
    return m_state.apply_action(action);
  }

 private:
  std::vector<int8_t> m_allowed_territories;
  const RiskState<NUM_PLAYERS> &m_state;
};

// Simplified reinforce set: each draw places all reserves on a single owned
// territory (one draw per owned territory). The full space of distributions
// is C(units + territories - 1, territories - 1); see
// mcts::RankedActionSet<mcts::PlaceNElementsIntoKBinsStateSpace> if the full
// space is needed.
template <size_t NUM_PLAYERS>
struct ReinforceActionSet {
  explicit ReinforceActionSet(const RiskState<NUM_PLAYERS> &state)
      : m_state(state) {
    for (size_t i = 0; i < state.m_map.size(); ++i) {
      if (state.m_map[i].owner == state.m_current_player) {
        m_allowed_territories.push_back(static_cast<int8_t>(i));
      }
    }
    m_num_units = state.m_reserves[state.m_current_player];
  }

  auto next(std::mt19937 &) -> std::optional<RiskState<NUM_PLAYERS>> {
    if (m_allowed_territories.empty() || m_num_units <= 0) {
      return std::nullopt;
    }
    ReinforceAction ra;
    ra.units_to_place.fill(0);
    ra.units_to_place[m_allowed_territories.back()] = m_num_units;
    m_allowed_territories.pop_back();
    PlayerAction action;
    action.reinforce_action = ra;
    return m_state.apply_action(action);
  }

 private:
  const RiskState<NUM_PLAYERS> &m_state;
  std::vector<int8_t> m_allowed_territories;
  int m_num_units;
};

// One bit per directed neighbor edge (kAllNeighborEdges contains both
// directions of every border). A set bit is an attack the current player can
// still queue from this set.
template <size_t NUM_PLAYERS>
struct QueueAttackActionSet {
  explicit QueueAttackActionSet(const RiskState<NUM_PLAYERS> &state)
      : m_state(state) {
    size_t index = 0;
    for (const auto &edge : kAllNeighborEdges) {
      Country src = edge.first;
      Country tgt = edge.second;
      if (state.m_map[static_cast<size_t>(src)].owner ==
              state.m_current_player &&
          state.m_map[static_cast<size_t>(tgt)].owner !=
              state.m_current_player &&
          state.m_map[static_cast<size_t>(src)].units > 1) {
        m_source_target_pairs.set(index);
      }
      ++index;
    }
  }

  auto any() const -> bool { return m_source_target_pairs.any(); }

  // Resolves the whole battle deterministically using the expected remnants
  // lookup: the attacker commits all units minus one and fights until the
  // target is captured or the source is down to one unit.
  auto next(std::mt19937 &) -> std::optional<RiskState<NUM_PLAYERS>> {
    size_t index = 0;
    for (const auto &edge : kAllNeighborEdges) {
      if (!m_source_target_pairs.test(index)) {
        ++index;
        continue;
      }
      m_source_target_pairs.set(index, false);
      const size_t src = static_cast<size_t>(edge.first);
      const size_t tgt = static_cast<size_t>(edge.second);

      const int attackers =
          static_cast<int>(m_state.m_map[src].units) - 1;  // keep one behind
      const int defenders = static_cast<int>(m_state.m_map[tgt].units);
      BattleRemnants outcome = LookupExpectedRemnants(attackers, defenders);

      auto result = m_state;
      // Both expectations are marginals and can both be positive for close
      // battles; the side with more expected survivors wins.
      if (outcome.attackers > outcome.defenders) {
        // Target captured: surviving attackers move in, losses stay behind.
        result.m_map[src].units -= (attackers - outcome.attackers);
        result.m_map[tgt].units = outcome.attackers;
        result.m_map[tgt].owner = result.m_current_player;
      } else {
        // Attack repelled: source down to one unit, defenders remain.
        result.m_map[src].units = 1;
        result.m_map[tgt].units = outcome.defenders;
      }
      result.m_first_attack_of_turn = false;
      return result;
    }
    return std::nullopt;
  }

 private:
  const RiskState<NUM_PLAYERS> &m_state;
  std::bitset<kAllNeighborEdges.size()> m_source_target_pairs;
};

// Defense is deterministic: always defend with the maximum allowed dice.
template <size_t NUM_PLAYERS>
struct QueueDefenseActionSet {
  explicit QueueDefenseActionSet(const RiskState<NUM_PLAYERS> &state)
      : m_state(state) {}

  auto next(std::mt19937 &) -> std::optional<RiskState<NUM_PLAYERS>> {
    if (m_consumed) {
      return std::nullopt;
    }
    m_consumed = true;
    QueueDefenseAction action{
        .num_defend_dice = std::min(
            2, static_cast<int>(
                   m_state.m_map[m_state.queued_attack->target].units))};
    return m_state.apply_action(action);
  }

 private:
  const RiskState<NUM_PLAYERS> &m_state;
  bool m_consumed = false;
};

// Simplified fortify: one bit per directed edge between two territories of
// the current player where the source has units to spare. Each draw moves all
// units minus one along the edge; a final draw ends the turn without moving
// anything, so the set is never empty. (Proper Risk fortify requires
// connectivity, not just adjacency, and allows moving any number of units.)
template <size_t NUM_PLAYERS>
struct FortifyActionSet {
  explicit FortifyActionSet(const RiskState<NUM_PLAYERS> &state)
      : m_state(state) {
    size_t index = 0;
    for (const auto &edge : kAllNeighborEdges) {
      const size_t src = static_cast<size_t>(edge.first);
      const size_t tgt = static_cast<size_t>(edge.second);
      if (state.m_map[src].owner == state.m_current_player &&
          state.m_map[tgt].owner == state.m_current_player &&
          state.m_map[src].units > 1) {
        m_source_target_pairs.set(index);
      }
      ++index;
    }
  }

  auto next(std::mt19937 &) -> std::optional<RiskState<NUM_PLAYERS>> {
    size_t index = 0;
    for (const auto &edge : kAllNeighborEdges) {
      if (!m_source_target_pairs.test(index)) {
        ++index;
        continue;
      }
      m_source_target_pairs.set(index, false);
      const size_t src = static_cast<size_t>(edge.first);
      const size_t tgt = static_cast<size_t>(edge.second);
      FortifyAction action{
          .source = static_cast<int>(src),
          .target = static_cast<int>(tgt),
          .num_units = static_cast<int>(m_state.m_map[src].units) - 1};
      return m_state.apply_action(action);
    }
    if (!m_end_turn_drawn) {
      m_end_turn_drawn = true;
      // num_units <= 1 is the "move nothing, just end the turn" encoding
      // (see RiskState::FortifyToMoveUnits).
      return m_state.apply_action(
          FortifyAction{.source = 0, .target = 0, .num_units = 0});
    }
    return std::nullopt;
  }

 private:
  const RiskState<NUM_PLAYERS> &m_state;
  std::bitset<kAllNeighborEdges.size()> m_source_target_pairs;
  bool m_end_turn_drawn = false;
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
  std::optional<ReinforceActionSet<NUM_PLAYERS>> reinforce_action;
  std::optional<QueueAttackActionSet<NUM_PLAYERS>> attack_action;
  std::optional<FortifyActionSet<NUM_PLAYERS>> fortify_action;

  auto next(std::mt19937 &gen) -> std::optional<RiskState<NUM_PLAYERS>> {
    if (reinforce_action.has_value()) {
      if (auto state = reinforce_action->next(gen)) {
        return state;
      }
      reinforce_action.reset();
    }
    if (attack_action.has_value()) {
      if (auto state = attack_action->next(gen)) {
        return state;
      }
      attack_action.reset();
    }
    if (fortify_action.has_value()) {
      if (auto state = fortify_action->next(gen)) {
        return state;
      }
      fortify_action.reset();
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
auto ActionSet(const RiskState<NUM_PLAYERS> &state)
    -> RiskActionSet<NUM_PLAYERS> {
  // Initial placement
  if (state.m_initial_placement) {
    return InitialPlaceActionSet<NUM_PLAYERS>(state);
  }

  // Process queued attack
  if (state.queued_attack.has_value()) {
    if (!state.queued_defense.has_value()) {
      return QueueDefenseActionSet<NUM_PLAYERS>(state);
    }
    CHECK(false) << "Not enumerating dice actions";
  }

  // Built in place in the variant: moving a locally built PlayerActionSet in
  // trips a -Wmaybe-uninitialized false positive on GCC 13.
  RiskActionSet<NUM_PLAYERS> result{
      std::in_place_type<PlayerActionSet<NUM_PLAYERS>>};
  auto &pa = std::get<PlayerActionSet<NUM_PLAYERS>>(result);
  if (state.m_first_attack_of_turn &&
      state.m_reserves[state.m_current_player] > 0) {
    pa.reinforce_action.emplace(state);
  } else {
    // Reserves for this turn are placed, so ending the turn is a legal move
    // from here on, whether or not attacks remain.
    pa.fortify_action.emplace(state);
  }

  QueueAttackActionSet<NUM_PLAYERS> attacks(state);
  if (attacks.any()) {
    pa.attack_action.emplace(std::move(attacks));
  }

  return result;
}

// Draws one successor state from the action set without replacement.
// std::nullopt means the set is exhausted.
template <size_t NUM_PLAYERS>
auto SampleSuccessor(RiskActionSet<NUM_PLAYERS> &action_set, std::mt19937 &gen)
    -> std::optional<RiskState<NUM_PLAYERS>> {
  return std::visit([&](auto &set) { return set.next(gen); }, action_set);
}

}  // namespace risk_game

#endif  // RISK_GAME_AI_CPP_RISK_RISK_GAME_STATE_SAMPLING_H
