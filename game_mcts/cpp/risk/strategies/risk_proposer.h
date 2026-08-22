#ifndef RISK_GAME_AI_CPP_RISK_STRATEGIES_RISK_PROPOSER_H
#define RISK_GAME_AI_CPP_RISK_STRATEGIES_RISK_PROPOSER_H

// Action-proposal policy for RiskState (see mcts::ActionProposer).
//
// This is deliberately *outside* the game: RiskState defines the rules and the
// dice, this defines which of a player's many legal actions the search and the
// playouts ever get to consider. Swapping it changes how Risk is played
// without touching how Risk works, and two proposers can be pitted against
// each other in one tournament.

#include <algorithm>
#include <array>
#include <bit>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <random>

#include "game_mcts/cpp/risk/risk_board.h"
#include "game_mcts/cpp/risk/risk_game.h"
#include "game_mcts/cpp/mcts/game_traits.h"

namespace risk_game {

// The stock policy, extracted unchanged from the old RiskState::sample_action.
// Known biases, documented so they are chosen rather than inherited:
//  - reserves are scattered i.i.d. uniformly over *all* owned territories, so
//    no proposal ever concentrates them on the border;
//  - attacks always commit the maximum dice;
//  - the fortify move is a single deterministic greedy choice.
//
// BORDER_REINFORCE changes the first bias: reserves land only on border
// territories (owned territories adjacent to an enemy), so reinforcements
// concentrate where they can fight. ATTACK_FAVORABLE changes the second:
// attacks are only launched from territories that outnumber the defender
// (suicidal attacks are never proposed); if none qualify the turn ends
// (unless a reinforcement is still pending, which forces a fallback attack).
template <size_t NUM_PLAYERS, bool BORDER_REINFORCE = false,
          bool ATTACK_FAVORABLE = false>
struct RiskProposer {
  using game_t = RiskState<NUM_PLAYERS>;
  using action_t = typename game_t::action_t;

  // Tree expansion: repeated sample() draws with duplicates rejected, stopping
  // exactly once support_size() actions have been handed out. Only the
  // reinforce branch is unbounded; there, progressive widening is what bounds
  // the node.
  auto propose(const game_t &state) const
      -> mcts::DedupSampler<RiskProposer, game_t> {
    return mcts::MakeDedupSampler(*this, state);
  }

  // Exact count of the distinct actions sample() can return from |state|, or
  // mcts::kUnknownSupport when the space is too large to enumerate. Mirrors
  // the branch structure of sample() below; keep the two in step.
  auto support_size(const game_t &state) const -> std::size_t {
    // Initial placement: one action per claimable territory.
    if (state.m_initial_placement) {
      const int8_t wanted_owner =
          state.m_num_initial_placements >= kNumTerritories
              ? state.m_current_player
              : -1;
      std::size_t count = 0;
      for (const Territory &t : state.m_map) {
        count += (t.owner == wanted_owner);
      }
      return count;
    }

    // Defense is deterministic: always the maximum allowed dice.
    if (state.queued_attack.has_value()) {
      return 1;
    }

    // Reinforcement scatters each reserve unit independently over the owned
    // territories, so the number of distinct placements is
    // C(reserves + owned - 1, owned - 1) — far too many to enumerate.
    if (state.m_first_attack_of_turn &&
        state.m_reserves[state.m_current_player] > 0) {
      return mcts::kUnknownSupport;
    }

    // Otherwise: one PlayerAction per attackable edge, plus the single
    // deterministic fortify that ends the turn. With no attacks available the
    // formula degenerates to exactly that fortify. ATTACK_FAVORABLE mirrors
    // sample()'s filter: only edges whose source outnumbers the target count.
    uint64_t mine;
    uint64_t strong;
    internal::OwnershipMasks(state.m_map.data(), state.m_map.size(),
                             state.m_current_player, mine, strong);
    std::size_t num_candidates = 0;
    uint64_t sources = mine & strong;
    while (sources != 0) {
      const int src = std::countr_zero(sources);
      sources &= sources - 1;
      uint64_t targets = kNeighborMasks[src] & ~mine;
      if constexpr (ATTACK_FAVORABLE) {
        while (targets != 0) {
          const int tgt = std::countr_zero(targets);
          targets &= targets - 1;
          num_candidates +=
              (state.m_map[src].units - 1 > state.m_map[tgt].units) ? 1 : 0;
        }
      } else {
        num_candidates += static_cast<std::size_t>(std::popcount(targets));
      }
    }
    return num_candidates + 1;
  }

  // One i.i.d. draw for playouts: no allocation, no dedup set.
  auto sample(const game_t &state, std::mt19937 &gen) const -> action_t {
    // Initial placement
    if (state.m_initial_placement) {
      // Before all territories are claimed, place on an unowned one;
      // afterwards only on own territories.
      const int8_t wanted_owner =
          state.m_num_initial_placements >= kNumTerritories
              ? state.m_current_player
              : -1;
      int territory_to_place = -1;
      uint32_t territory_score = 0;
      for (size_t i = 0; i < state.m_map.size(); ++i) {
        if (state.m_map[i].owner == wanted_owner) {
          const uint32_t val = gen();
          if (territory_to_place < 0 || val > territory_score) {
            territory_score = val;
            territory_to_place = static_cast<int>(i);
          }
        }
      }
      return InitialPlaceAction{territory_to_place};
    }

    // A queued attack without a queued defense is the defender's decision
    // node. (With both queued it is a chance node, handled by the game's
    // sample_chance_action, and this proposer is never asked.)
    if (state.queued_attack.has_value()) {
      assert(!state.queued_defense.has_value());
      return QueueDefenseAction{std::min(
          2, static_cast<int>(state.m_map[state.queued_attack->target].units))};
    }

    PlayerAction pa;

    // Ownership masks shared by the reinforce and attack scans below: bit i of
    // |mine| is set iff the current player owns territory i, bit i of |strong|
    // iff territory i has units to spare.
    uint64_t mine;
    uint64_t strong;
    internal::OwnershipMasks(state.m_map.data(), state.m_map.size(),
                             state.m_current_player, mine, strong);

    if (state.m_first_attack_of_turn) {
      const int reserves = state.m_reserves[state.m_current_player];
      if (reserves > 0) {
        // Each reserve unit lands independently and uniformly at random on an
        // owned territory (same distribution as the previous 0/1-weighted
        // std::discrete_distribution, without its allocation and setup).
        std::array<uint8_t, kNumTerritories> owned_territories;
        int num_owned = 0;
        // Ascending set-bit order, identical to scanning the map for
        // territories owned by the current player (keeps the RNG draws below
        // landing on the same territories as the previous full scan).
        uint64_t owned = mine;
        while (owned != 0) {
          owned_territories[num_owned++] =
              static_cast<uint8_t>(std::countr_zero(owned));
          owned &= owned - 1;
        }
        if constexpr (BORDER_REINFORCE) {
          // Restrict the placement set to border territories (those with at
          // least one enemy neighbor). Falls back to all owned territories
          // when none are on the border (cannot happen mid-turn in practice,
          // but keeps the sampler total).
          std::array<uint8_t, kNumTerritories> border_territories;
          int num_border = 0;
          for (int i = 0; i < num_owned; ++i) {
            if ((kNeighborMasks[owned_territories[i]] & ~mine) != 0) {
              border_territories[num_border++] = owned_territories[i];
            }
          }
          if (num_border > 0) {
            owned_territories = border_territories;
            num_owned = num_border;
          }
        }
        ReinforceAction ra{};
        for (int i = 0; i < reserves; ++i) {
          ra.units_to_place[owned_territories[internal::UniformBelow(
              gen, static_cast<uint32_t>(num_owned))]] += 1;
        }
        pa.reinforce_action = ra;
      }
    }

    // Collect all attack candidates (owned source with units to spare,
    // non-owned target), then pick one uniformly at random with a single RNG
    // call (same uniform distribution as picking the max of per-edge random
    // draws, but without a draw per edge). Iterates the set bits of the
    // ownership masks instead of scanning all directed edges. Each candidate
    // is packed as src << 8 | tgt (one store per candidate).
    std::array<uint16_t, kAllNeighborEdges.size()> candidates;
    int num_candidates = 0;
    const uint64_t enemy = ~mine;
    uint64_t sources = mine & strong;
    while (sources != 0) {
      const int src = std::countr_zero(sources);
      sources &= sources - 1;
      uint64_t targets = kNeighborMasks[src] & enemy;
      while (targets != 0) {
        candidates[num_candidates++] =
            static_cast<uint16_t>((src << 8) | std::countr_zero(targets));
        targets &= targets - 1;
      }
    }
    // Ending the turn is one more option alongside the individual attacks, so
    // the player can stop attacking while attacks are still available (drawn
    // uniformly with them, i.e. with probability 1/(num_candidates + 1)).
    // Not offered while |pa| still carries this turn's reinforcement: that
    // PlayerAction has to be applied before the turn can end, or the reserves
    // it places would be dropped.
    const bool may_end_turn = !pa.reinforce_action.has_value();
    if (num_candidates > 0) {
      if constexpr (ATTACK_FAVORABLE) {
        // Keep only attacks whose source outnumbers the target, drawn
        // uniformly with the end-turn option as in stock (probability
        // 1/(num_favorable + 1) when end turn is allowed). support_size()
        // mirrors this filter exactly (the DedupSampler asserts
        // support_size() never over-reports).
        std::array<uint16_t, kAllNeighborEdges.size()> favorable;
        int num_favorable = 0;
        for (int i = 0; i < num_candidates; ++i) {
          const int src = candidates[i] >> 8;
          const int tgt = candidates[i] & 0xFF;
          if (state.m_map[src].units - 1 > state.m_map[tgt].units) {
            favorable[num_favorable++] = candidates[i];
          }
        }
        if (num_favorable > 0) {
          const uint32_t num_options =
              static_cast<uint32_t>(num_favorable) + (may_end_turn ? 1U : 0U);
          const int pick =
              static_cast<int>(internal::UniformBelow(gen, num_options));
          if (pick < num_favorable) {
            const uint16_t candidate = favorable[pick];
            const int src = candidate >> 8;
            pa.attack_action = QueueAttackAction{
                src, candidate & 0xFF, std::min(3, state.m_map[src].units - 1)};
          }
          // Otherwise end turn: fall through to the fortify below.
        } else if (!may_end_turn) {
          // A pending reinforcement must be applied before the turn can end,
          // so attack anyway: uniform over all candidates, as in stock.
          const uint16_t candidate =
              candidates[static_cast<int>(internal::UniformBelow(
                  gen, static_cast<uint32_t>(num_candidates)))];
          const int src = candidate >> 8;
          pa.attack_action = QueueAttackAction{
              src, candidate & 0xFF, std::min(3, state.m_map[src].units - 1)};
        }
        // Otherwise no favorable attack: fall through to the fortify below.
      } else {
        const uint32_t num_options =
            static_cast<uint32_t>(num_candidates) + (may_end_turn ? 1U : 0U);
        const int pick =
            static_cast<int>(internal::UniformBelow(gen, num_options));
        if (pick < num_candidates) {
          const uint16_t candidate = candidates[pick];
          const int src = candidate >> 8;
          pa.attack_action = QueueAttackAction{
              src, candidate & 0xFF, std::min(3, state.m_map[src].units - 1)};
        }
      }
    }

    if (pa.attack_action.has_value() || pa.reinforce_action.has_value()) {
      return pa;
    }
    // Turn over, either by choice or because no attack is possible: fortify
    // from the safest territory to the most threatened one, then end the turn.
    FortifyAction fa;

    struct SourceCandidate {
      size_t country_id = 0;
      // Must lose to any real candidate: the default stands in only until the
      // first owned territory is seen. (A finite sentinel like 1000 would win
      // against owned territories with more enemy-neighbor units, producing a
      // fortify source the player does not own.)
      int num_enemy_neighbors = std::numeric_limits<int>::max();
      int num_units = -1;
      auto operator<(const SourceCandidate &other) const -> bool {
        if (num_enemy_neighbors != other.num_enemy_neighbors) {
          return num_enemy_neighbors > other.num_enemy_neighbors;
        }
        return num_units < other.num_units;
      }
    };

    struct TargetCandidate {
      size_t country_id = 0;
      int num_enemy_neighbors = -1;
      int num_units = 1000;
      auto operator<(const TargetCandidate &other) const -> bool {
        if (num_enemy_neighbors != other.num_enemy_neighbors) {
          return num_enemy_neighbors < other.num_enemy_neighbors;
        }
        return other.num_units < num_units;
      }
    };

    SourceCandidate best_source{};
    TargetCandidate best_target{};

    // Iterate owned territories in ascending order via the ownership mask
    // (same order as the previous full-map scan, so max-selection ties resolve
    // identically), and sum enemy-neighbor units via the neighbor bitmask
    // (integer addition is order-independent, so the sums match exactly).
    uint64_t owned = mine;
    while (owned != 0) {
      const size_t i = static_cast<size_t>(std::countr_zero(owned));
      owned &= owned - 1;
      int enemy_neighbors_units = 0;
      uint64_t enemy_neighbors = kNeighborMasks[i] & ~mine;
      while (enemy_neighbors != 0) {
        const size_t neighbor =
            static_cast<size_t>(std::countr_zero(enemy_neighbors));
        enemy_neighbors &= enemy_neighbors - 1;
        enemy_neighbors_units += state.m_map[neighbor].units;
      }
      TargetCandidate tc{.country_id = i,
                         .num_enemy_neighbors = enemy_neighbors_units,
                         .num_units = state.m_map[i].units};
      SourceCandidate sc{.country_id = i,
                         .num_enemy_neighbors = enemy_neighbors_units,
                         .num_units = state.m_map[i].units};
      best_source = std::max(best_source, sc);
      best_target = std::max(best_target, tc);
    }
    fa.source = best_source.country_id;
    fa.target = best_target.country_id;
    fa.num_units = state.m_map[fa.source].units - 1;
    return fa;
  }
};

static_assert(mcts::ActionProposer<RiskProposer<2>, RiskState<2>>);
static_assert(mcts::ActionProposer<RiskProposer<3>, RiskState<3>>);

}  // namespace risk_game

#endif  // RISK_GAME_AI_CPP_RISK_STRATEGIES_RISK_PROPOSER_H
