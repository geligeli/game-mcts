#include "cpp/risk/risk_serialization.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <limits>
#include <random>

#include "cpp/risk/risk_game.h"
#include "cpp/risk/strategies/risk_proposer.h"

namespace risk_game {
namespace {

template <size_t NUM_PLAYERS>
using traits_t = mcts::GameSerializationTraits<RiskState<NUM_PLAYERS>>;

template <size_t NUM_PLAYERS>
auto RoundTripState(const RiskState<NUM_PLAYERS> &state)
    -> RiskState<NUM_PLAYERS> {
  return traits_t<NUM_PLAYERS>::StateFromProto(
      traits_t<NUM_PLAYERS>::StateToProto(state));
}

template <size_t NUM_PLAYERS>
auto RoundTripAction(const RiskAction &action) -> RiskAction {
  return traits_t<NUM_PLAYERS>::ActionFromProto(
      traits_t<NUM_PLAYERS>::ActionToProto(action));
}

// Plays seeded random games and, at every step, roundtrips both the current
// state and the sampled action through the proto. Tracks phase coverage so
// the test fails if random play stops visiting a node kind.
template <size_t NUM_PLAYERS>
void PlayRandomGamesWithRoundtrips(uint32_t seed, int num_games,
                                   int max_steps) {
  const RiskProposer<NUM_PLAYERS> proposer{};
  std::mt19937 gen(seed);
  bool saw_initial_placement = false;
  bool saw_queued_attack = false;
  bool saw_chance_node = false;
  bool saw_terminal = false;
  for (int game_idx = 0; game_idx < num_games; ++game_idx) {
    RiskState<NUM_PLAYERS> state;
    for (int step = 0; step < max_steps; ++step) {
      saw_initial_placement |= state.m_initial_placement;
      saw_queued_attack |= state.queued_attack.has_value();
      saw_chance_node |= state.is_chance_node();

      EXPECT_EQ(RoundTripState(state), state)
          << "game " << game_idx << " step " << step;

      const RiskAction action = state.is_chance_node()
                                    ? state.sample_chance_action(gen)
                                    : proposer.sample(state, gen);
      EXPECT_EQ(RoundTripAction<NUM_PLAYERS>(action), action)
          << "game " << game_idx << " step " << step;

      state = state.apply_action(action);
      if (mcts::is_terminal(state.current_state())) {
        saw_terminal = true;
        EXPECT_EQ(RoundTripState(state), state);
        break;
      }
    }
  }
  EXPECT_TRUE(saw_initial_placement);
  EXPECT_TRUE(saw_queued_attack);
  EXPECT_TRUE(saw_chance_node);
  EXPECT_TRUE(saw_terminal);
}

// A mid-game state: initial placement over, alternating ownership, 5 units
// everywhere, player 0 to move at the start of their turn with no reserves.
auto MakeMidGameState() -> RiskState<2> {
  RiskState<2> state;
  state.m_initial_placement = false;
  state.m_num_initial_placements = kNumTerritories;
  state.m_reserves = {0, 0};
  for (size_t i = 0; i < state.m_map.size(); ++i) {
    state.m_map[i].owner = static_cast<int8_t>(i % 2);
    state.m_map[i].units = 5;
  }
  state.m_current_player = 0;
  state.m_first_attack_of_turn = true;
  return state;
}

}  // namespace

TEST(RiskSerializationTest, RandomGamesRoundTripTwoPlayers) {
  PlayRandomGamesWithRoundtrips<2>(/*seed=*/123, /*num_games=*/10,
                                   /*max_steps=*/20000);
}

TEST(RiskSerializationTest, RandomGamesRoundTripThreePlayers) {
  PlayRandomGamesWithRoundtrips<3>(/*seed=*/456, /*num_games=*/10,
                                   /*max_steps=*/20000);
}

TEST(RiskSerializationTest, InitialStateRoundTrips) {
  EXPECT_EQ(RoundTripState(RiskState<2>{}), RiskState<2>{});
  EXPECT_EQ(RoundTripState(RiskState<3>{}), RiskState<3>{});
}

TEST(RiskSerializationTest, SaturatedTerritoryRoundTrips) {
  RiskState<2> state = MakeMidGameState();
  state.m_map[0].units = std::numeric_limits<uint16_t>::max();
  state.m_map[1].units = std::numeric_limits<uint16_t>::max();
  EXPECT_EQ(RoundTripState(state), state);
}

TEST(RiskSerializationTest, QueuedAttackRoundTrips) {
  RiskState<2> state = MakeMidGameState();
  state.queued_attack =
      QueueAttackAction{.source = 7, .target = 0, .num_attack_dice = 3};
  state.m_current_player = state.m_map[0].owner;
  state.m_first_attack_of_turn = false;
  EXPECT_EQ(RoundTripState(state), state);
}

TEST(RiskSerializationTest, ChanceNodeRoundTrips) {
  RiskState<2> state = MakeMidGameState();
  state.queued_attack =
      QueueAttackAction{.source = 7, .target = 0, .num_attack_dice = 1};
  state.queued_defense = QueueDefenseAction{.num_defend_dice = 2};
  state.m_current_player = -1;
  state.m_first_attack_of_turn = false;
  ASSERT_TRUE(state.is_chance_node());
  EXPECT_EQ(RoundTripState(state), state);
}

TEST(RiskSerializationTest, TerminalStateRoundTrips) {
  RiskState<2> state = MakeMidGameState();
  for (Territory &t : state.m_map) {
    t.owner = 1;
  }
  ASSERT_TRUE(mcts::is_terminal(state.current_state()));
  EXPECT_EQ(RoundTripState(state), state);
}

TEST(RiskSerializationTest, ActionVariantsRoundTrip) {
  ReinforceAction reinforce{};
  reinforce.units_to_place[0] = 3;
  reinforce.units_to_place[kNumTerritories - 1] =
      std::numeric_limits<uint16_t>::max();

  const RiskAction actions[] = {
      RiskAction{InitialPlaceAction{.territory = 41}},
      RiskAction{PlayerAction{}},
      RiskAction{PlayerAction{.reinforce_action = reinforce}},
      RiskAction{PlayerAction{.attack_action =
                                  QueueAttackAction{.source = 7,
                                                    .target = 0,
                                                    .num_attack_dice = 3}}},
      RiskAction{PlayerAction{
          .reinforce_action = reinforce,
          .attack_action =
              QueueAttackAction{
                  .source = 7, .target = 0, .num_attack_dice = 1}}},
      RiskAction{QueueDefenseAction{.num_defend_dice = 2}},
      RiskAction{FortifyAction{.source = 0, .target = 7, .num_units = 100}},
      RiskAction{RollDiceAction{.attacker_rolls = {6, 5, 4},
                                .defender_rolls = {6, 6}}},
      RiskAction{RollDiceAction{.attacker_rolls = {3, 0, 0},
                                .defender_rolls = {2, 0}}},
  };
  for (const RiskAction &action : actions) {
    EXPECT_EQ(RoundTripAction<2>(action), action);
    EXPECT_EQ(RoundTripAction<3>(action), action);
  }
}

TEST(RiskSerializationTest, NumPlayersMismatchFails) {
  const auto proto = traits_t<3>::StateToProto(RiskState<3>{});
  EXPECT_DEATH(traits_t<2>::StateFromProto(proto), "Check failed");
}

TEST(RiskSerializationTest, WrongTerritoryCountFails) {
  auto proto = traits_t<2>::StateToProto(RiskState<2>{});
  proto.mutable_territories()->RemoveLast();
  EXPECT_DEATH(traits_t<2>::StateFromProto(proto), "Check failed");
}

}  // namespace risk_game
