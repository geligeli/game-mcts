#include "web/risk_match.h"

#include <string>

#include "game_mcts/core/mcts/policies.h"
#include "game_mcts/games/risk/risk_board.h"
#include "game_mcts/games/risk/strategies/risk_proposer.h"
#include "game_mcts/games/risk/strategies/risk_rollout_shortcuts.h"
#include "gtest/gtest.h"

namespace risk_web {
namespace {

using proposer_t = risk_game::RiskProposer<2>;

// The reference bot, thinking less.
policy_t Bot() {
  auto rollout = mcts::MakeShortcutRollout<state_t, proposer_t>(
      &risk_game::ResolveBattleWithExpectationInPlace<2>);
  return mcts::tournament::MctsPolicy<state_t, proposer_t, decltype(rollout)>{
      .iterations_ = 30, .rollout = rollout};
}

bool Has(const std::string &json, const std::string &part) {
  return json.find(part) != std::string::npos;
}

// A border territory of |seat|'s with the most armies, and an enemy
// neighbour of it; -1s when there is none.
std::pair<int, int> Front(const state_t &state, int seat) {
  std::pair<int, int> best = {-1, -1};
  for (int t = 0; t < risk_game::kNumTerritories; ++t) {
    if (state.map_[t].owner != seat) {
      continue;
    }
    const risk_game::CountryData &country = risk_game::kBoard[t];
    for (std::size_t i = 0; i < country.neighbor_count_; ++i) {
      const int n = static_cast<int>(country.neighbors_[i]);
      if (state.map_[n].owner != seat &&
          (best.first < 0 ||
           state.map_[t].units > state.map_[best.first].units)) {
        best = {t, n};
      }
    }
  }
  return best;
}

// Runs the policy until it is the person's move again.
void BotUntilHuman(RiskMatch &match) {
  while (Has(match.State(), "\"phase\":\"bot\"")) {
    match.BotStep();
  }
}

TEST(RiskMatchTest, QuickSetupDealsTheWholeBoard) {
  RiskMatch match(Bot(), /*human_seat=*/0, /*max_rounds=*/40, 7);
  EXPECT_TRUE(Has(match.State(), "\"phase\":\"place\""));
  const std::string answer = match.QuickSetup();
  EXPECT_TRUE(Has(answer, "\"k\":\"setup\"")) << answer;
  EXPECT_FALSE(match.state().initial_placement_);
  for (const risk_game::Territory &t : match.state().map_) {
    EXPECT_GE(t.owner, 0);
  }
}

TEST(RiskMatchTest, PlacingATakenTerritoryIsRefusedAndChangesNothing) {
  RiskMatch match(Bot(), 0, 40, 1);
  EXPECT_FALSE(Has(match.Place(3), "\"error\""));
  BotUntilHuman(match);
  const state_t before = match.state();
  const std::string answer = match.Place(3);
  EXPECT_TRUE(Has(answer, "\"error\"")) << answer;
  EXPECT_EQ(match.state().map_, before.map_);
}

TEST(RiskMatchTest, AnAttackMustPlaceTheReservesFirst) {
  RiskMatch match(Bot(), 0, 40, 3);
  match.QuickSetup();
  BotUntilHuman(match);
  ASSERT_TRUE(Has(match.State(), "\"phase\":\"reinforce\""));
  const auto [source, target] = Front(match.state(), 0);
  const std::string answer = match.Attack(source, target, {}, false);
  EXPECT_TRUE(Has(answer, "all reserve armies must be placed")) << answer;
}

TEST(RiskMatchTest, BlitzStopsAtAConquestOrTheLastArmy) {
  RiskMatch match(Bot(), 0, 40, 5);
  match.QuickSetup();
  BotUntilHuman(match);
  const auto [source, target] = Front(match.state(), 0);
  placement_t reinforce{};
  reinforce[source] = match.state().reserves_[0];
  const std::string answer = match.Attack(source, target, reinforce, true);
  EXPECT_TRUE(Has(answer, "\"k\":\"roll\"")) << answer;
  EXPECT_FALSE(Has(answer, "\"error\"")) << answer;
  EXPECT_TRUE(match.state().map_[target].owner == 0 ||
              match.state().map_[source].units == 1);
}

TEST(RiskMatchTest, AFortifyEndsTheTurn) {
  RiskMatch match(Bot(), 1, 40, 9);
  match.QuickSetup();
  BotUntilHuman(match);
  placement_t reinforce{};
  reinforce[Front(match.state(), 1).first] = match.state().reserves_[1];
  const std::string answer = match.Fortify(0, 0, 0, reinforce);
  EXPECT_FALSE(Has(answer, "\"error\"")) << answer;
  EXPECT_TRUE(Has(answer, "\"k\":\"fortify\"")) << answer;
  EXPECT_TRUE(Has(answer, "\"phase\":\"bot\"")) << answer;
}

TEST(RiskMatchTest, AWholeGameEndsWithAResult) {
  for (const int seat : {0, 1}) {
    RiskMatch match(Bot(), seat, /*max_rounds=*/8, 11 + seat);
    match.set_fast_defense(seat == 1);
    match.QuickSetup();
    for (int turn = 0; turn < 200 && !Has(match.State(), "\"phase\":\"over\"");
         ++turn) {
      BotUntilHuman(match);
      if (Has(match.State(), "\"phase\":\"over\"")) {
        break;
      }
      const auto [source, target] = Front(match.state(), seat);
      placement_t reinforce{};
      reinforce[source] = match.state().reserves_[seat];
      if (match.state().map_[source].units + reinforce[source] > 1) {
        match.Attack(source, target, reinforce, true);
        reinforce = {};
      }
      if (!Has(match.State(), "\"phase\":\"over\"")) {
        EXPECT_FALSE(Has(match.Fortify(0, 0, 0, reinforce), "\"error\""));
      }
    }
    const std::string state = match.State();
    EXPECT_TRUE(Has(state, "\"phase\":\"over\"")) << state;
    EXPECT_FALSE(Has(state, "\"result\":\"\"")) << state;
  }
}

TEST(BoardJsonTest, NamesNeighboursAndContinents) {
  const std::string board = BoardJson();
  EXPECT_TRUE(Has(board, "\"Afghanistan\""));
  EXPECT_TRUE(Has(board, "{\"name\":\"Asia\",\"bonus\":7"));
}

}  // namespace
}  // namespace risk_web
