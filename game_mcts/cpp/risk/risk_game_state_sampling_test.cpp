#include "cpp/risk/risk_game_state_sampling.h"

#include <gtest/gtest.h>

#include <variant>

#include "cpp/risk/risk_game.h"

namespace risk_game {

namespace {

auto CountUnits(const RiskState<2> &state) -> int {
  int total = 0;
  for (const Territory &t : state.m_map) {
    total += t.units;
  }
  return total;
}

auto MakeAlternatingOwnershipGame() -> RiskState<2> {
  RiskState<2> game;
  game.m_initial_placement = false;
  for (int8_t i = 0; i < kNumTerritories; ++i) {
    game.m_map[i] = {.owner = static_cast<int8_t>(i % 2),
                     .units = 2};  // Alternate ownership
  }
  game.turn_count = kNumTerritories * 2;
  return game;
}

}  // namespace

TEST(RiskGameStateSampling, InitialPlacementIsFirstActionSet) {
  RiskState<2> game;
  RiskActionSet<2> action_set = ActionSet(game);
  ASSERT_TRUE(std::holds_alternative<InitialPlaceActionSet<2>>(action_set));

  std::mt19937 gen(42);
  auto successor = SampleSuccessor(action_set, gen);
  ASSERT_TRUE(successor.has_value());
  EXPECT_EQ(CountUnits(*successor), 1);
}

TEST(RiskGameStateSampling, ReinforceAndAttackSet) {
  RiskState<2> game = MakeAlternatingOwnershipGame();
  game.m_reserves = {5, 5};

  RiskActionSet<2> action_set = ActionSet(game);
  ASSERT_TRUE(std::holds_alternative<PlayerActionSet<2>>(action_set));

  const auto &player_set = std::get<PlayerActionSet<2>>(action_set);
  EXPECT_TRUE(player_set.reinforce_action.has_value());
  EXPECT_TRUE(player_set.attack_action.has_value());
}

TEST(RiskGameStateSampling, ReinforceIsDrawnFirstAndConsumesAllReserves) {
  RiskState<2> game = MakeAlternatingOwnershipGame();
  game.m_reserves = {5, 5};

  RiskActionSet<2> action_set = ActionSet(game);
  std::mt19937 gen(42);
  auto successor = SampleSuccessor(action_set, gen);
  ASSERT_TRUE(successor.has_value());

  // All 5 reserves were placed on a single territory of player 0.
  EXPECT_EQ(successor->m_reserves[0], 0);
  EXPECT_EQ(CountUnits(*successor), CountUnits(game) + 5);
}

TEST(RiskGameStateSampling, AttackResolvesExpectedBattleOutcome) {
  RiskState<2> game = MakeAlternatingOwnershipGame();
  game.m_reserves = {0, 0};
  game.m_first_attack_of_turn = false;

  RiskActionSet<2> action_set = ActionSet(game);
  ASSERT_TRUE(std::holds_alternative<PlayerActionSet<2>>(action_set));
  const auto &player_set = std::get<PlayerActionSet<2>>(action_set);
  ASSERT_FALSE(player_set.reinforce_action.has_value());
  ASSERT_TRUE(player_set.attack_action.has_value());

  std::mt19937 gen(42);
  auto successor = SampleSuccessor(action_set, gen);
  ASSERT_TRUE(successor.has_value());

  // The successor must be a valid post-battle state: every territory has at
  // least one unit, and no battle left a territory empty.
  for (const Territory &t : successor->m_map) {
    EXPECT_GE(t.units, 1);
  }
  // A full battle was fought, so the total unit count must have dropped.
  EXPECT_LT(CountUnits(*successor), CountUnits(game));
}

TEST(RiskGameStateSampling, AttackSetExhausts) {
  RiskState<2> game = MakeAlternatingOwnershipGame();
  game.m_reserves = {0, 0};
  game.m_first_attack_of_turn = false;

  QueueAttackActionSet<2> attack_set(game);
  std::mt19937 gen(42);

  int draws = 0;
  while (attack_set.next(gen).has_value()) {
    ++draws;
  }
  // Every directed edge from a current-player territory with units to spare
  // into an enemy territory is attackable.
  size_t expected = 0;
  for (const auto &edge : kAllNeighborEdges) {
    if (game.m_map[static_cast<size_t>(edge.first)].owner ==
            game.m_current_player &&
        game.m_map[static_cast<size_t>(edge.second)].owner !=
            game.m_current_player) {
      ++expected;
    }
  }
  EXPECT_GT(expected, 0u);
  EXPECT_EQ(draws, expected);
}

// Ending the turn must be reachable while attacks are still on the table,
// otherwise the search is forced to attack until it physically cannot.
TEST(RiskGameStateSampling, FortifyIsOfferedAlongsideRemainingAttacks) {
  RiskState<2> game = MakeAlternatingOwnershipGame();
  game.m_reserves = {0, 0};
  game.m_first_attack_of_turn = false;

  RiskActionSet<2> action_set = ActionSet(game);
  ASSERT_TRUE(std::holds_alternative<PlayerActionSet<2>>(action_set));
  const auto &player_set = std::get<PlayerActionSet<2>>(action_set);
  EXPECT_TRUE(player_set.attack_action.has_value());
  EXPECT_TRUE(player_set.fortify_action.has_value());
}

// The reinforce set has to be consumed before the turn can end, or the
// unplaced reserves would be silently dropped.
TEST(RiskGameStateSampling, FortifyIsWithheldWhileReinforceIsPending) {
  RiskState<2> game = MakeAlternatingOwnershipGame();
  game.m_reserves = {5, 5};

  RiskActionSet<2> action_set = ActionSet(game);
  ASSERT_TRUE(std::holds_alternative<PlayerActionSet<2>>(action_set));
  const auto &player_set = std::get<PlayerActionSet<2>>(action_set);
  ASSERT_TRUE(player_set.reinforce_action.has_value());
  EXPECT_FALSE(player_set.fortify_action.has_value());
}

TEST(RiskGameStateSampling, NoAttackYieldsFortifySet) {
  RiskState<2> game;
  game.m_initial_placement = false;
  game.m_first_attack_of_turn = false;
  game.m_reserves = {0, 0};
  // Player 0 owns everything except territory 0 (player 1, not eliminated).
  // All of player 0's territories bordering territory 0 have a single unit,
  // so no attack is possible (attacks require units > 1).
  for (int8_t i = 0; i < kNumTerritories; ++i) {
    game.m_map[i] = {.owner = 0, .units = 2};
  }
  game.m_map[0] = {.owner = 1, .units = 2};
  for (const auto &edge : kAllNeighborEdges) {
    if (static_cast<size_t>(edge.second) == 0) {
      game.m_map[static_cast<size_t>(edge.first)].units = 1;
    }
  }

  RiskActionSet<2> action_set = ActionSet(game);
  ASSERT_TRUE(std::holds_alternative<PlayerActionSet<2>>(action_set));
  const auto &player_set = std::get<PlayerActionSet<2>>(action_set);
  ASSERT_FALSE(player_set.attack_action.has_value());
  ASSERT_TRUE(player_set.fortify_action.has_value());

  std::mt19937 gen(42);
  auto successor = SampleSuccessor(action_set, gen);
  ASSERT_TRUE(successor.has_value());
  // Fortify conserves units and ends the turn.
  EXPECT_EQ(CountUnits(*successor), CountUnits(game));
  EXPECT_EQ(successor->m_current_player, 1);
}

// The fortify set always offers at least the "move nothing, end the turn"
// draw, so a player with no legal troop movement still has a move.
TEST(RiskGameStateSampling, FortifySetAlwaysOffersEndTurn) {
  RiskState<2> game;
  game.m_initial_placement = false;
  game.m_first_attack_of_turn = false;
  game.m_reserves = {0, 0};
  // Player 0 holds a single one-unit territory: no fortify edge has a source
  // with units to spare.
  for (int8_t i = 0; i < kNumTerritories; ++i) {
    game.m_map[i] = {.owner = 1, .units = 2};
  }
  game.m_map[0] = {.owner = 0, .units = 1};

  FortifyActionSet<2> fortify_set(game);
  std::mt19937 gen(42);
  auto successor = fortify_set.next(gen);
  ASSERT_TRUE(successor.has_value());
  EXPECT_EQ(CountUnits(*successor), CountUnits(game));
  EXPECT_EQ(successor->m_current_player, 1);
  EXPECT_FALSE(fortify_set.next(gen).has_value());
}

}  // namespace risk_game
