#include "game_mcts/cpp/mcts/minimax.h"

#include <gtest/gtest.h>

#include "game_mcts/cpp/tictactoe/tictactoe.h"

namespace minimax {

/*
Action mapping:
 0 | 1 | 2
---+---+---
 3 | 4 | 5
---+---+---
 6 | 7 | 8
*/
TEST(MinimaxTest, TicTacToeNextPositionWins) {
  tictactoe::TicTacToe game;
  game = game.apply_action(0).apply_action(1).apply_action(3).apply_action(4);
  EXPECT_EQ(ComputeActionValue(game, 6, 0), 1.0f);

  game = game.apply_action(2);

  /*
    X | O | X
   ---+---+---
    X | O |
   ---+---+---
      |   |
  */
  EXPECT_EQ(ComputeActionValue(game, 7, 0), -1.0f);
}

TEST(MinimaxTest, BlunderLeadsToLoss) {
  tictactoe::TicTacToe game;
  game = game.apply_action(0).apply_action(1).apply_action(3).apply_action(4);
  /*
    X | O | X <- this move is a blunder
   ---+---+---
    X | O |
   ---+---+---
      |   |
  */
  EXPECT_EQ(ComputeActionValue(game, 2, 0), -1.0f);
}

TEST(MinimaxTest, TicTacToeIsAlwaysADraw) {
  tictactoe::TicTacToe game;
  auto valid_moves = game.valid_moves();
  for (int action : valid_moves.actions) {
    EXPECT_EQ(ComputeActionValue(game, action, 0), 0.0f);
  }
}

TEST(MinimaxTest, TicTacToeIsAlwaysADrawBinaryOutcome) {
  tictactoe::TicTacToe game;
  auto valid_moves = game.valid_moves();
  for (int action : valid_moves.actions) {
    EXPECT_EQ(ComputeActionValueBinaryOutcome(game, action, 0), 0);
  }
}

// Regression: the binary variant once started max_value at
// -numeric_limits<int>::infinity(), which is 0 for int, so losing moves (-1)
// never registered and every lost position evaluated as a draw.
TEST(MinimaxTest, BinaryOutcomeDetectsLostPosition) {
  // X at middle-left; O to move. Playing the top-middle edge loses by force:
  // X:0 (corner), O must block 6, X:4 (center) creates a double threat.
  tictactoe::TicTacToe game;
  game = game.apply_action(3);
  EXPECT_EQ(ComputeActionValueBinaryOutcome(game, 1, 1), -1);
  // The center still draws.
  EXPECT_EQ(ComputeActionValueBinaryOutcome(game, 4, 1), 0);
}

}  // namespace minimax
