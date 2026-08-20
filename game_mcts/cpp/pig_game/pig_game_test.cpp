#include "game_mcts/cpp/pig_game/pig_game.h"

#include <gtest/gtest.h>

#include <boost/math/distributions/chi_squared.hpp>
#include <iostream>

#include "game_mcts/cpp/mcts/mcts.inl"

namespace pig_game {

TEST(PigGameTest, InitialState) {
  PigGame game;
  EXPECT_EQ(game.current_player(), 0);
  EXPECT_FALSE(game.is_chance_node());
  auto moves = game.valid_moves();
  EXPECT_EQ(moves.actions.size(), 1);  // Roll at the start
}

auto calculate_p_value(const std::array<int, 6> &counts) -> double {
  // 1. Total number of rolls
  int n = std::accumulate(counts.begin(), counts.end(), 0);

  // Safety check: avoid division by zero or invalid stats
  if (n <= 0) {
    return 1.0;
  }

  // 2. Expected frequency for uniform distribution
  const double expected = static_cast<double>(n) / 6.0;

  // 3. Calculate Chi-Squared Statistic: sum((O - E)^2 / E)
  double chi_sq_stat = 0.0;
  for (int observed : counts) {
    double diff = static_cast<double>(observed) - expected;
    chi_sq_stat += (diff * diff) / expected;
  }

  // 4. Use Boost to find the p-value
  // Degrees of Freedom (df) = categories - 1 = 5
  const boost::math::chi_squared dist(5);
  // we use 'complement' to get the upper tail (p-value) directly
  double r = boost::math::cdf(boost::math::complement(dist, chi_sq_stat));
  return r;
}

// Test that the chance-game concept is properly satisfied
TEST(PigGameTest, ChanceGameConcept) {
  static_assert(mcts::ChanceGame<PigGame>,
                "PigGame should satisfy the ChanceGame concept");

  PigGame game;
  // Initial state is a decision node (player must roll)
  EXPECT_FALSE(game.is_chance_node());

  // After rolling, we're at a chance node
  PigGame after_roll = game.apply_action(0);  // ACT_ROLL = 0
  EXPECT_TRUE(after_roll.is_chance_node());

  // Chance probabilities should be 1/6 for each die face. The die is rules,
  // so it comes from the game itself, not from a proposer.
  std::array<int, 6> counts{};
  std::mt19937 gen(42);
  for (int i = 0; i < 10000; ++i) {
    int action = after_roll.sample_chance_action(gen);
    EXPECT_GE(action, 1);
    EXPECT_LE(action, 6);
    counts[action - 1]++;
  }
  EXPECT_GT(calculate_p_value(counts), 0.05)
      << "Die roll distribution did not converge";
}

TEST(PigGameTest, IsValidAction) {
  PigGame game;
  std::string reason;

  // Decision node: only ACT_ROLL(0) and ACT_HOLD(1) are legal ids.
  reason = "sentinel";
  EXPECT_TRUE(game.is_valid_action(0, reason));
  EXPECT_EQ(reason, "sentinel");  // Success leaves |reason| untouched.
  EXPECT_TRUE(game.is_valid_action(1, reason));
  EXPECT_FALSE(game.is_valid_action(2, reason));
  EXPECT_FALSE(reason.empty());
  EXPECT_FALSE(game.is_valid_action(-1, reason));
  EXPECT_FALSE(reason.empty());

  // Chance node: the action is the die-roll result, 1..6.
  PigGame chance = game.apply_action(0);  // Roll -> waiting for the die.
  ASSERT_TRUE(chance.is_chance_node());
  for (int roll = 1; roll <= 6; ++roll) {
    EXPECT_TRUE(chance.is_valid_action(roll, reason)) << roll;
  }
  EXPECT_FALSE(chance.is_valid_action(0, reason));
  EXPECT_FALSE(reason.empty());
  EXPECT_FALSE(chance.is_valid_action(7, reason));
  EXPECT_FALSE(reason.empty());

  // Everything valid_moves proposes is legal, across a full random game.
  // PlayoutStep picks the right sampler per node kind: the die at chance
  // nodes, the proposer at decision nodes.
  std::mt19937 gen(42);
  const mcts::DefaultProposer<PigGame> proposer;
  while (!mcts::is_terminal(game.current_state())) {
    auto moves = game.valid_moves();
    for (int move : moves.actions) {
      EXPECT_TRUE(game.is_valid_action(move, reason)) << move;
    }
    mcts::PlayoutStep(game, proposer, gen);
  }
}

// Test that MCTS with stochastic node picker can run on PigGame
TEST(PigGameTest, MctsBasicIteration) {
  PigGame game;
  mcts::MctsRunner<PigGame> runner(game);

  std::mt19937 gen(42);
  auto picker = mcts::MctsStochasticNodePicker<PigGame>(gen);

  // Run a few iterations to make sure it doesn't crash
  for (int i = 0; i < 100; ++i) {
    runner.OneIteration(picker, gen);
  }

  EXPECT_GT(runner.node_storage.size(), 1);
  EXPECT_GT(runner.node_storage[0].num_visits, 0);
}

// Test MCTS convergence toward known optimal Pig strategy
// The optimal strategy for Pig is to hold at approximately 20 points.
// This test verifies that MCTS finds sensible hold thresholds.
TEST(PigGameTest, MctsConvergesToOptimalStrategy) {
  // Test multiple game states to verify MCTS behavior
  std::mt19937 gen(12345);
  const int kIterations = 10000;

  // Test 1: At turn_total ~20, MCTS should generally prefer hold
  // We simulate this by creating a game state where holding is beneficial
  {
    // Start fresh and make some moves to reach a state with decent turn
    PigGame game;

    // Roll and get a good result (not 1)
    game = game.apply_action(0);  // Roll
    game = game.apply_action(6);  // Die roll result = 6

    game = game.apply_action(0);  // Roll again
    game = game.apply_action(5);  // Die roll result = 5

    game = game.apply_action(0);  // Roll again
    game = game.apply_action(4);  // Die roll result = 4

    game = game.apply_action(0);  // Roll again
    game = game.apply_action(5);  // Die roll result = 5

    // Now we have turn_total = 20, game should NOT be a chance node
    EXPECT_FALSE(game.is_chance_node());

    // Run MCTS
    mcts::MctsRunner<PigGame> runner(game);
    auto picker = mcts::MctsStochasticNodePicker<PigGame>(gen);

    for (int i = 0; i < kIterations; ++i) {
      runner.OneIteration(picker, gen);
    }

    // At turn_total = 20, MCTS should have explored both hold and roll
    // and found that hold is often reasonable
    auto moves = game.valid_moves();
    EXPECT_EQ(moves.actions.size(), 2);  // Should have both Roll and Hold

    // Get visit counts for roll and hold actions
    const auto &root = runner.node_storage[0];
    int roll_visits = 0;
    int hold_visits = 0;

    const auto &player_node =
        std::get<mcts::MctsStochasticNode<PigGame>::PlayerNode>(
            root.node_variant);

    for (const auto &child : player_node.children) {
      if (child.action == 0)
        roll_visits = runner.node_storage[child.node_index].num_visits;
      else if (child.action == 1)
        hold_visits = runner.node_storage[child.node_index].num_visits;
    }

    // Both actions should be explored significantly
    EXPECT_GT(roll_visits, 0) << "Roll action should be explored";
    EXPECT_GT(hold_visits, 0) << "Hold action should be explored";

    std::cout << "At turn_total=20: Roll visits=" << roll_visits
              << ", Hold visits=" << hold_visits << std::endl;
  }

  // Test 2: At very low turn_total (e.g., 2), MCTS should prefer rolling
  {
    PigGame game;
    game = game.apply_action(0);  // Roll
    game = game.apply_action(2);  // Die roll result = 2

    // Now we have turn_total = 2
    EXPECT_FALSE(game.is_chance_node());

    mcts::MctsRunner<PigGame> runner(game);
    auto picker = mcts::MctsStochasticNodePicker<PigGame>(gen);

    for (int i = 0; i < kIterations; ++i) {
      runner.OneIteration(picker, gen);
    }

    const auto &root = runner.node_storage[0];
    int roll_visits = 0;
    int hold_visits = 0;

    const auto &player_node =
        std::get<mcts::MctsStochasticNode<PigGame>::PlayerNode>(
            root.node_variant);

    for (const auto &child : player_node.children) {
      if (child.action == 0)
        roll_visits = runner.node_storage[child.node_index].num_visits;
      else if (child.action == 1)
        hold_visits = runner.node_storage[child.node_index].num_visits;
    }

    // At low turn total, MCTS should strongly prefer rolling
    std::cout << "At turn_total=2: Roll visits=" << roll_visits
              << ", Hold visits=" << hold_visits << std::endl;

    // Rolling should be explored more since holding 2 points is rarely optimal
    EXPECT_GT(roll_visits, hold_visits)
        << "At turn_total=2, rolling should be preferred over holding";
  }
}

// Test that MCTS best_action works with stochastic games
TEST(PigGameTest, MctsBestAction) {
  PigGame game;
  // Roll to get to a decision point with some turn_total
  game = game.apply_action(0);  // Roll
  game = game.apply_action(4);  // Get 4

  mcts::MctsRunner<PigGame> runner(game);
  std::mt19937 gen(42);
  auto picker = mcts::MctsStochasticNodePicker<PigGame>(gen);

  for (int i = 0; i < 1000; ++i) {
    runner.OneIteration(picker, gen);
  }

  int best = runner.best_action();
  // Best action should be either roll (0) or hold (1)
  EXPECT_TRUE(best == 0 || best == 1);
}

// Test proper handling of terminal states through MCTS
TEST(PigGameTest, MctsHandlesTerminalState) {
  std::mt19937 gen(42);

  // Play a game to completion using MCTS
  PigGame game;
  int moves_made = 0;
  const int kMaxMoves = 10000;  // Safety limit

  while (!mcts::is_terminal(game.current_state()) && moves_made < kMaxMoves) {
    if (game.is_chance_node()) {
      // At chance nodes, just sample uniformly (simulating die roll)
      auto moves = game.valid_moves();
      std::uniform_int_distribution<size_t> dist(0, moves.actions.size() - 1);
      game = game.apply_action(moves.actions[dist(gen)]);
    } else {
      // At decision nodes, use MCTS to pick action
      mcts::MctsRunner<PigGame> runner(game);
      auto picker = mcts::MctsStochasticNodePicker<PigGame>(gen);

      // Run MCTS for a modest number of iterations
      for (int i = 0; i < 100; ++i) {
        runner.OneIteration(picker, gen);
      }

      // Get best action (if no children yet, just roll)
      const auto &root = runner.node_storage[0];
      const auto &player_node =
          std::get<mcts::MctsStochasticNode<PigGame>::PlayerNode>(
              root.node_variant);
      if (player_node.children.empty()) {
        game = game.apply_action(0);  // Roll
      } else {
        game = game.apply_action(runner.best_action());
      }
    }
    moves_made++;
  }

  // Game should have ended
  EXPECT_LT(moves_made, kMaxMoves) << "Game should complete within move limit";
  EXPECT_TRUE(mcts::is_terminal(game.current_state()));
}

TEST(PigGameTest, MctsFindsWinningMove) {
  PigGame game;
  // Player 0 has 90 points, turn total is 10. Goal is 100.
  // Holding gives 100 points -> win.
  // Rolling risks losing the turn.
  game.SetState(90, 80, 10, 0, false);

  mcts::MctsRunner<PigGame> runner(game);
  std::mt19937 gen(42);
  auto picker = mcts::MctsStochasticNodePicker<PigGame>(gen);

  for (int i = 0; i < 100; ++i) {
    runner.OneIteration(picker, gen);
  }

  // Expect hold (action 1) to be the best action
  EXPECT_EQ(runner.best_action(), 1);
}

}  // namespace pig_game
