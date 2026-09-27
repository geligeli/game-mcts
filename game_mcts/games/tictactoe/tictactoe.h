#ifndef GAME_MCTS_GAME_MCTS_GAMES_TICTACTOE_TICTACTOE_H
#define GAME_MCTS_GAME_MCTS_GAMES_TICTACTOE_TICTACTOE_H
#include <array>
#include <cstdint>
#include <ostream>
#include <random>
#include <string>
#include <vector>

#include "game_mcts/core/mcts/game_traits.h"

namespace tictactoe {

/*
Action mapping:
 0 | 1 | 2
---+---+---
 3 | 4 | 5
---+---+---
 6 | 7 | 8
*/
struct TicTacToe {
  using action_t = int;
  int current_player_{0};
  uint32_t board_state_{};  // 18 bits:
  using action_type = int;
  static constexpr std::size_t kNumPlayers = 2;

  int current_player() const;
  TicTacToe apply_action(action_t action) const;
  // In-place variant of apply_action, for scratch-state hot loops (rollouts).
  void apply_action_in_place(action_t action);
  // The game's default decision policy, picked up by mcts::DefaultProposer as
  // an allocation-free alternative to materializing valid_moves().
  action_t SampleAction(std::uniform_random_bit_generator auto& gen) const;
  mcts::game_state_t current_state() const;
  mcts::VectorLegalActionSet<action_t> valid_moves() const;
  // Legality oracle for referee/debug paths (not hot loops). On failure sets
  // |reason| to a short explanation; on success leaves it untouched.
  bool is_valid_action(const action_t& action, std::string& reason) const;
};

static_assert(mcts::Game<TicTacToe>);
// mcts::PlayoutStep prefers the in-place transition in the rollout hot loop.
static_assert(mcts::InPlaceGame<TicTacToe>);
static_assert(
    mcts::ActionProposer<mcts::DefaultProposer<TicTacToe>, TicTacToe>);

std::ostream& operator<<(std::ostream& os, const TicTacToe& game);

auto TicTacToe::SampleAction(std::uniform_random_bit_generator auto& gen) const
    -> action_t {
  // Uniform over the free cells, without materializing the move list.
  const uint32_t occupied =
      (board_state_ & 0x1FF) | ((board_state_ >> 9) & 0x1FF);
  std::array<uint8_t, 9> free_cells;
  int num_free = 0;
  for (int i = 0; i < 9; ++i) {
    if (!((occupied >> i) & 1)) {
      free_cells[num_free++] = static_cast<uint8_t>(i);
    }
  }
  std::uniform_int_distribution<int> dist(0, num_free - 1);
  return free_cells[dist(gen)];
}

}  // namespace tictactoe

#endif  // GAME_MCTS_GAME_MCTS_GAMES_TICTACTOE_TICTACTOE_H
