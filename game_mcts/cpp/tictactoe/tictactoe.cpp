#include "game_mcts/cpp/tictactoe/tictactoe.h"

namespace tictactoe {

auto TicTacToe::current_player() const -> int { return current_player_; }

auto TicTacToe::apply_action(int action) const -> TicTacToe {
  TicTacToe next = *this;
  next.apply_action_in_place(action);
  return next;
}

void TicTacToe::apply_action_in_place(int action) {
  if (current_player_ == 0) {
    board_state |= (1u << action);
  } else {
    board_state |= (1u << (action + 9));
  }
  current_player_ = 1 - current_player_;
}

auto TicTacToe::current_state() const -> mcts::game_state_t {
  const uint32_t win_masks[8] = {
      0b000000111,  // Row 0
      0b000111000,  // Row 1
      0b111000000,  // Row 2
      0b001001001,  // Col 0
      0b010010010,  // Col 1
      0b100100100,  // Col 2
      0b100010001,  // Diag ''
      0b001010100   // Diag /
  };

  for (const auto &mask : win_masks) {
    if ((board_state & mask) == mask) {
      return mcts::win_t{0};
    } else if (((board_state >> 9) & mask) == mask) {
      return mcts::win_t{1};
    }
  }

  // Check for draw or ongoing
  if (((board_state & 0x1FF) | ((board_state >> 9) & 0x1FF)) == 0x1FF) {
    return mcts::draw_t{};
  }
  return mcts::ongoing_t{};
}

auto TicTacToe::valid_moves() const -> mcts::VectorLegalActionSet<action_t> {
  mcts::VectorLegalActionSet<action_t> result;
  for (int i = 0; i < 9; ++i) {
    bool occupied = ((board_state >> i) & 1) || ((board_state >> (i + 9)) & 1);
    if (!occupied) {
      result.actions.push_back(i);
    }
  }
  return result;
}

auto TicTacToe::is_valid_action(const action_t &action,
                                std::string &reason) const -> bool {
  if (!std::holds_alternative<mcts::ongoing_t>(current_state())) {
    reason = "game is already over";
    return false;
  }
  if (action < 0 || action > 8) {
    reason = "action out of range [0, 8]";
    return false;
  }
  // board_state: bits 0-8 player 0, bits 9-17 player 1.
  if (((board_state >> action) & 1) || ((board_state >> (action + 9)) & 1)) {
    reason = "cell is already occupied";
    return false;
  }
  return true;
}

auto operator<<(std::ostream &os, const TicTacToe &game) -> std::ostream & {
  for (int r = 0; r < 3; ++r) {
    for (int c = 0; c < 3; ++c) {
      int idx = r * 3 + c;
      char mark = '.';
      if ((game.board_state >> idx) & 1) {
        mark = 'X';
      } else if ((game.board_state >> (idx + 9)) & 1) {
        mark = 'O';
      }
      os << mark << ' ';
    }
    os << '\n';
  }
  os << "Next player: " << (game.current_player_ == 0 ? "X(=0)" : "O(=1)")
     << '\n';
  return os;
}

}  // namespace tictactoe
