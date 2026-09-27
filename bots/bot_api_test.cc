// Pins the submission contract: parameter parsing, and that a submission
// written against bot_api.h compiles and plays legal risk2 moves.

#include "bots/bot_api.h"

#include <random>
#include <string>

#include "gtest/gtest.h"

namespace {

using candidate::Params;

// A submission under test: the contract's required entry point, cheap enough
// to play a few hundred moves.
candidate::policy_t MakeTestPolicy(const Params &params) {
  using game_t = candidate::game_t;
  using proposer_t = risk_game::RiskProposer<candidate::kNumPlayers>;
  auto rollout = mcts::MakeShortcutRollout<game_t, proposer_t>(
      &risk_game::ResolveBattleWithExpectationInPlace<candidate::kNumPlayers>);
  return mcts::tournament::MctsPolicy<game_t, proposer_t, decltype(rollout)>{
      .iterations_ = params.GetInt("iterations", 5), .rollout = rollout};
}

TEST(ParamsTest, ParsesKeysAndValues) {
  const Params params = Params::Parse("iterations=800,widening_c=1.5");
  EXPECT_EQ(params.GetInt("iterations", 0), 800);
  EXPECT_DOUBLE_EQ(params.GetDouble("widening_c", 0.0), 1.5);
  EXPECT_EQ(params.Values().size(), 2u);
}

TEST(ParamsTest, EmptySpecYieldsNoValues) {
  EXPECT_TRUE(Params::Parse("").Values().empty());
  EXPECT_TRUE(Params::Parse(",,").Values().empty());
}

// A knob the submission does not understand, or one the arena mistyped, must
// never stop it from starting -- it drops out of the tournament otherwise.
TEST(ParamsTest, MalformedInputFallsBackInsteadOfThrowing) {
  const Params params = Params::Parse("stray,=novalue,iterations=abc,c=1.5x");
  EXPECT_EQ(params.GetInt("iterations", 400), 400);
  EXPECT_DOUBLE_EQ(params.GetDouble("c", 2.0), 2.0);
  EXPECT_EQ(params.GetInt("absent", 7), 7);
  EXPECT_EQ(params.get("absent", "default"), "default");
  EXPECT_FALSE(params.contains("stray"));
  // The malformed entries still parsed as *present*, just not as numbers.
  EXPECT_TRUE(params.contains("iterations"));
}

TEST(ParamsTest, LastValueWinsForARepeatedKey) {
  EXPECT_EQ(Params::Parse("n=1,n=2").GetInt("n", 0), 2);
}

// The contract's real promise: a type-erased policy_t built from an arbitrary
// policy type drives legal moves, with chance nodes resolved by the rules.
TEST(BotApiTest, PolicyPlaysLegalRisk2Moves) {
  const candidate::policy_t policy = MakeTestPolicy(Params::Parse(""));

  std::mt19937 gen(1234);
  candidate::game_t game;
  int decisions = 0;
  for (int step = 0; step < 300 && !mcts::is_terminal(game.current_state());
       ++step) {
    if (game.is_chance_node()) {
      game = game.apply_action(game.sample_chance_action(gen));
      continue;
    }
    const auto decision = policy(game, gen);
    std::string reason;
    ASSERT_TRUE(game.is_valid_action(decision.action, reason))
        << "illegal action at step " << step << ": " << reason;
    game = decision.successor;
    ++decisions;
  }
  EXPECT_GT(decisions, 0);
}

}  // namespace
