#include "game_mcts/cpp/tictactoe/tictactoe.h"

#include <gtest/gtest.h>

#include <iostream>
#include <random>

namespace tictactoe {

TEST(TictactoeTest, InitialState) {
  TicTacToe game;
  std::cout << game << std::endl;
}

TEST(TictactoeTest, SimpleGame) {
  TicTacToe game;
  std::mt19937 gen{};

  while (!(mcts::is_terminal(game.current_state()))) {
    auto moves = game.valid_moves();
    ASSERT_FALSE(moves.empty());
    game = game.apply_action(
        *moves.next(game, gen));  // Always pick first valid move
    std::cout << game << std::endl;
  }
}

TEST(TictactoeTest, IsValidAction) {
  TicTacToe game;
  std::string reason;

  // Out-of-range actions are rejected with a reason.
  EXPECT_FALSE(game.is_valid_action(-1, reason));
  EXPECT_FALSE(reason.empty());
  EXPECT_FALSE(game.is_valid_action(9, reason));
  EXPECT_FALSE(reason.empty());

  // Every proposed move on a fresh board is legal, and a successful check
  // leaves |reason| untouched.
  reason = "sentinel";
  auto moves = game.valid_moves();
  ASSERT_EQ(moves.actions.size(), 9);
  for (int move : moves.actions) {
    EXPECT_TRUE(game.is_valid_action(move, reason)) << move;
    EXPECT_EQ(reason, "sentinel");
  }

  // Occupied cell is rejected.
  game = game.apply_action(4);
  EXPECT_FALSE(game.is_valid_action(4, reason));
  EXPECT_FALSE(reason.empty());

  // Every proposed move stays legal throughout a full game.
  std::mt19937 gen(42);
  while (!(mcts::is_terminal(game.current_state()))) {
    auto vm = game.valid_moves();
    for (int move : vm.actions) {
      EXPECT_TRUE(game.is_valid_action(move, reason)) << move;
    }
    game = game.apply_action(*vm.next(game, gen));
  }

  // No move is legal once the game is over.
  EXPECT_FALSE(game.is_valid_action(0, reason));
  EXPECT_FALSE(reason.empty());
}

}  // namespace tictactoe