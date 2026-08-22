#include <gtest/gtest.h>

#include <algorithm>
#include <random>
#include <vector>

#include "absl/debugging/failure_signal_handler.h"
#include "absl/debugging/symbolize.h"
#include "absl/log/log.h"
#include "cpp/risk/risk_game.h"
#include "cpp/risk/strategies/risk_proposer.h"
#include "cpp/risk/strategies/risk_rollout_shortcuts.h"
#include "game_mcts/cpp/mcts/mcts.inl"

namespace risk_game {

using risk_game_t = RiskState<2>;
using proposer_t = RiskProposer<2>;

TEST(RiskGameTest, IsChanceGame) {
  static_assert(mcts::ChanceGame<risk_game_t>,
                "RiskGame should satisfy the ChanceGame concept");
  static_assert(mcts::ActionProposer<proposer_t, risk_game_t>,
                "RiskProposer should propose actions for RiskGame");
}

TEST(RiskGameTest, IsStochasticNode) {
  mcts::MctsStochasticNode<risk_game_t, proposer_t> node(risk_game_t{},
                                                         proposer_t{});
}

TEST(RiskGameTest, OneIteration) {
  std::mt19937 gen(42);
  const auto rollout_policy =
      mcts::RandomRollout<risk_game_t, proposer_t>{.max_steps = 50000};
  mcts::MctsRunner<risk_game_t, proposer_t, decltype(rollout_policy)> runner(
      risk_game_t{}, proposer_t{}, rollout_policy);
  auto picker = mcts::MctsStochasticNodePicker<risk_game_t>(gen);
  runner.OneIteration(picker, gen);
  std::cerr << runner << std::endl;
}

TEST(RiskGameTest, OneHundredIterations) {
  std::mt19937 gen(42);
  // Cap playout length: with saturated unit counts, random rollouts are not
  // guaranteed to terminate; over-cap games score as draws.
  const auto rollout_policy =
      mcts::RandomRollout<risk_game_t, proposer_t>{.max_steps = 50000};
  mcts::MctsRunner<risk_game_t, proposer_t, decltype(rollout_policy)> runner(
      risk_game_t{}, proposer_t{}, rollout_policy);
  auto picker = mcts::MctsStochasticNodePicker<risk_game_t>(gen);
  for (int i = 0; i < 1000; ++i) {
    runner.OneIteration(picker, gen);
    std::cerr << i << std::endl;
  }
  std::cerr << runner << std::endl;
}

TEST(RiskGameTest, SelfPlayWithAsciiBoard) {
  std::mt19937 gen(42);
  risk_game_t game;

  constexpr int kIterationsPerMove = 20;
  constexpr int kMaxMoves = 60;  // Safety cap; random rollouts dominate cost.

  int move = 0;
  while (!mcts::is_terminal(game.current_state()) && move < kMaxMoves) {
    LOG(INFO) << "Move " << move << ":\n" << game;
    if (game.is_chance_node()) {
      // Dice rolls: no decision to make, just sample the outcome.
      game = game.apply_action(game.sample_chance_action(gen));
    } else {
      // Rollouts resolve battles with the expectation table (cheaper);
      // the game tree itself uses exact logic.
      const auto rollout_policy =
          mcts::MakeShortcutRollout<risk_game_t, proposer_t>(
              &ResolveBattleWithExpectation<2>);
      mcts::MctsRunner<risk_game_t, proposer_t, decltype(rollout_policy)>
          runner(game, proposer_t{}, rollout_policy);
      auto picker = mcts::MctsStochasticNodePicker<risk_game_t>(gen);
      for (int i = 0; i < kIterationsPerMove; ++i) {
        runner.OneIteration(picker, gen);
      }
      game = game.apply_action(runner.best_action());
    }
    ++move;
  }
  LOG(INFO) << "Final state after " << move << " moves:\n" << game;
  LOG(INFO) << "Result: "
            << (mcts::is_terminal(game.current_state()) ? "game over"
                                                        : "move cap reached");
}

TEST(RiskGameTest, ThreePlayerMcts) {
  using risk_3p_t = RiskState<3>;
  static_assert(mcts::num_players_v<risk_3p_t> == 3);

  std::mt19937 gen(42);
  using proposer_3p_t = RiskProposer<3>;
  const auto rollout_policy =
      mcts::MakeShortcutRollout<risk_3p_t, proposer_3p_t>(
          &ResolveBattleWithExpectation<3>);
  mcts::MctsRunner<risk_3p_t, proposer_3p_t, decltype(rollout_policy)> runner(
      risk_3p_t{}, proposer_3p_t{}, rollout_policy);
  auto picker = mcts::MctsStochasticNodePicker<risk_3p_t>(gen);
  for (int i = 0; i < 200; ++i) {
    runner.OneIteration(picker, gen);
  }

  const auto &root = runner.node_storage[0];
  EXPECT_EQ(root.num_visits, 200);
  // Every rollout scores +1 for the winner and -1 for the two losers, so all
  // components sum to -num_visits.
  double sum = 0.0;
  for (double v : root.total_value) {
    sum += v;
  }
  EXPECT_DOUBLE_EQ(sum, -200.0);

  // A valid action must be selected.
  const auto action = runner.best_action();
  risk_3p_t next = risk_3p_t{}.apply_action(action);
  (void)next;
}

TEST(RiskGameTest, ThreePlayerSelfPlay) {
  std::mt19937 gen(42);
  RiskState<3> game;

  constexpr int kIterationsPerMove = 10;
  constexpr int kMaxMoves = 40;  // Smoke test only.

  int move = 0;
  while (!mcts::is_terminal(game.current_state()) && move < kMaxMoves) {
    if (game.is_chance_node()) {
      game = game.apply_action(game.sample_chance_action(gen));
    } else {
      const auto rollout_policy =
          mcts::MakeShortcutRollout<RiskState<3>, RiskProposer<3>>(
              &ResolveBattleWithExpectation<3>);
      mcts::MctsRunner<RiskState<3>, RiskProposer<3>, decltype(rollout_policy)>
          runner(game, RiskProposer<3>{}, rollout_policy);
      auto picker = mcts::MctsStochasticNodePicker<RiskState<3>>(gen);
      for (int i = 0; i < kIterationsPerMove; ++i) {
        runner.OneIteration(picker, gen);
      }
      game = game.apply_action(runner.best_action());
    }
    ++move;
  }
  EXPECT_GT(move, 10);  // Game actually progresses.
}

}  // namespace risk_game
