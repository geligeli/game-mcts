#include "problem/risk_session.h"

#include <algorithm>
#include <cstddef>
#include <random>
#include <string>

#include "game_mcts/core/mcts/serialization.h"
#include "game_mcts/games/risk/risk_game.h"
#include "game_mcts/games/risk/risk_serialization.h"
#include "game_mcts/games/risk/strategies/risk_proposer.h"
#include "gtest/gtest.h"

namespace tournament_broker {
namespace {

using risk_game::RiskAction;
using risk_game::RiskState;
using traits = mcts::GameSerializationTraits<RiskState<2>>;

constexpr int kAlaska = static_cast<int>(risk_game::Country::Alaska);
constexpr int kAlberta = static_cast<int>(risk_game::Country::Alberta);
constexpr int kKamchatka = static_cast<int>(risk_game::Country::Kamchatka);

// Past initial placement, |rounds| full rounds in, player 0 about to place 3
// armies. Territories 0..|p0_territories|-1 are player 0's, the rest player
// 1's, with |p0_units| and |p1_units| armies each.
RiskState<2> Board(int rounds, int p0_territories, int p0_units = 2,
                   int p1_units = 2) {
  RiskState<2> state;
  state.initial_placement_ = false;
  state.num_initial_placements_ = 80;
  state.turn_count_ = 40 + rounds;
  state.current_player_ = 0;
  state.reserves_ = {3, 0};
  for (int i = 0; i < risk_game::kNumTerritories; ++i) {
    const bool mine = i < p0_territories;
    state.map_[i] = {
        .owner = static_cast<int8_t>(mine ? 0 : 1),
        .units = static_cast<uint16_t>(mine ? p0_units : p1_units)};
  }
  return state;
}

TEST(RiskSessionTest, CountsRoundsFromTheEndOfPlacement) {
  EXPECT_EQ(RiskSession::Rounds(RiskState<2>{}), 0);
  EXPECT_EQ(RiskSession::Rounds(Board(7, 21)), 7);
}

TEST(RiskSessionTest, UncappedOrBeforeTheCapTheGameGoesOn) {
  EXPECT_FALSE(RiskSession(0, Board(10000, 30)).Outcome().has_value());
  EXPECT_FALSE(RiskSession(100, Board(99, 30)).Outcome().has_value());
}

TEST(RiskSessionTest, AtTheCapMoreTerritoriesWinsOverMoreArmies) {
  // Player 1 holds 22 territories with 2 armies each against player 0's 20
  // with 9: territory first.
  const auto outcome = RiskSession(100, Board(100, 20, 9, 2)).Outcome();
  ASSERT_TRUE(outcome.has_value());
  EXPECT_FALSE(outcome->is_draw);
  EXPECT_EQ(outcome->winning_player, 1);
}

TEST(RiskSessionTest, AtTheCapEqualTerritoriesGoToMoreArmies) {
  const auto outcome = RiskSession(100, Board(100, 21, 3, 2)).Outcome();
  ASSERT_TRUE(outcome.has_value());
  EXPECT_FALSE(outcome->is_draw);
  EXPECT_EQ(outcome->winning_player, 0);
}

TEST(RiskSessionTest, AtTheCapAnExactTieIsADraw) {
  const auto outcome = RiskSession(100, Board(100, 21)).Outcome();
  ASSERT_TRUE(outcome.has_value());
  EXPECT_TRUE(outcome->is_draw);
}

TEST(RiskSessionTest, AWholeBoardStillWinsBeforeTheCap) {
  const auto outcome = RiskSession(100, Board(5, 42)).Outcome();
  ASSERT_TRUE(outcome.has_value());
  EXPECT_EQ(outcome->winning_player, 0);
}

// Player 0 (Alaska 5, Alberta 1) against player 1 (everything else, Kamchatka
// 3), four rounds in, player 0 about to place 3.
RiskState<2> Skirmish() {
  RiskState<2> state = Board(4, 0);
  state.map_[kAlaska] = {.owner = 0, .units = 5};
  state.map_[kAlberta] = {.owner = 0, .units = 1};
  state.map_[kKamchatka] = {.owner = 1, .units = 3};
  return state;
}

void Apply(RiskSession &session, const RiskAction &action) {
  std::string error;
  ASSERT_TRUE(session.ApplySerializedAction(
      traits::ActionToProto(action).SerializeAsString(), &error))
      << error;
}

RiskAction Roll(std::array<int, 3> attacker, std::array<int, 2> defender) {
  return risk_game::RollDiceAction{attacker, defender};
}

RiskAction Attack(int source, int target) {
  return risk_game::PlayerAction{
      .attack_action_ = risk_game::QueueAttackAction{source, target, 3}};
}

bool Has(const std::string &view, const std::string &part) {
  return view.find(part) != std::string::npos;
}

// Every step has a caption and a view: the board it left as JSON, with what
// the step touched and its dice, for the browser replay to draw.
TEST(RiskSessionTest, EachStepGetsACaptionAndAJsonView) {
  RiskSession session(100, Skirmish());
  const std::string start = session.RenderState();
  EXPECT_TRUE(start.starts_with("{\"r\":4,\"m\":100,\"p\":0,\"o\":\"100" +
                                std::string(39, '1') + "\",\"u\":[2,5,1,"))
      << start;
  EXPECT_TRUE(Has(start, "\"rv\":[3,0]")) << start;
  EXPECT_FALSE(Has(start, "\"a\"") || Has(start, "\"d\"")) << start;

  risk_game::ReinforceAction reinforce{};
  reinforce.units_to_place_[kAlaska] = 3;
  Apply(session,
        risk_game::PlayerAction{.reinforce_action_ = reinforce,
                                .attack_action_ = risk_game::QueueAttackAction{
                                    kAlaska, kKamchatka, 3}});
  EXPECT_TRUE(Has(session.RenderLastStep(), "attacks"));
  std::string view = session.RenderState();
  EXPECT_TRUE(Has(view, "\"u\":[2,8,1,")) << view;
  EXPECT_TRUE(Has(view, "\"h\":[1,20],\"a\":[1,20]")) << view;

  Apply(session, risk_game::QueueDefenseAction{2});
  EXPECT_TRUE(Has(session.RenderLastStep(), "defends"));
  EXPECT_TRUE(Has(session.RenderState(), "\"p\":-1")) << "the dice are next";
  Apply(session, Roll({4, 6, 5}, {5, 5}));
  view = session.RenderState();
  EXPECT_TRUE(Has(view, "\"d\":[[6,5,4],[5,5]]")) << "sorted as compared";
  EXPECT_FALSE(Has(view, "\"c\"")) << view;

  Apply(session, Attack(kAlaska, kKamchatka));
  Apply(session, risk_game::QueueDefenseAction{2});
  Apply(session, Roll({6, 1, 6}, {1, 1}));
  EXPECT_TRUE(Has(session.RenderLastStep(), "CONQUERED"));
  EXPECT_TRUE(Has(session.RenderState(), "\"c\":20")) << "Kamchatka";

  Apply(session, risk_game::FortifyAction{kAlaska, kAlberta, 2});
  EXPECT_TRUE(Has(session.RenderLastStep(),
                  "P0\x1b[0m's turn: 2 battles, took 1 (Kamchatka)"))
      << session.RenderLastStep();
  EXPECT_TRUE(Has(session.RenderState(), "\"a\":[1,2]"));
}

// The last view says how the game ended.
TEST(RiskSessionTest, TheLastViewHasTheResult) {
  RiskSession session(5, Board(4, 21));
  Apply(session, risk_game::FortifyAction{0, 0, 1});  // P0 ends its turn
  EXPECT_FALSE(Has(session.RenderState(), "\"w\""));
  Apply(session, risk_game::FortifyAction{0, 0, 1});  // P1 ends round 4
  ASSERT_TRUE(session.Outcome().has_value());
  EXPECT_TRUE(Has(session.RenderState(),
                  "\"w\":\"Round cap: a draw, territories and armies level\""))
      << session.RenderState();
}

// A whole game at the round cap: random play stalls the longest, so it is the
// worst case for the replay's size, which one gRPC message carries.
TEST(RiskSessionTest, AWholeCappedGameStaysSmall) {
  risk_game::RiskProposer<2> proposer;
  for (const uint32_t seed : {1u, 2u, 3u}) {
    RiskSession session(40);
    std::mt19937 gen(seed);
    std::size_t total = session.RenderState().size();
    std::size_t largest = 0;
    while (!session.Outcome().has_value()) {
      if (session.IsChanceNode()) {
        session.ApplyChanceAction(gen);
      } else {
        Apply(session, proposer.sample(session.State(), gen));
      }
      EXPECT_FALSE(session.RenderLastStep().empty());
      EXPECT_FALSE(session.RenderState().empty());
      largest = std::max(largest, session.RenderState().size());
      total += session.RenderLastStep().size() + session.RenderState().size();
    }
    EXPECT_LT(largest, 400u) << "seed " << seed;
    EXPECT_LT(total, 1u << 20) << "seed " << seed;
  }
}

}  // namespace
}  // namespace tournament_broker
