#include "game_mcts/cpp/pig_game/pig_game.h"

#include <cassert>

#include "game_mcts/cpp/mcts/game_traits.h"

namespace pig_game {

auto PigGame::current_player() const -> int { return player_turn; }

// 2. Identify if this is a stochastic node
auto PigGame::is_chance_node() const -> bool { return waiting_for_roll; }

void PigGame::SetState(int p0, int p1, int turn, int player, bool waiting) {
  p0_score = p0;
  p1_score = p1;
  turn_total = turn;
  player_turn = player;
  waiting_for_roll = waiting;
}

// 4. Generate Valid Moves
auto PigGame::valid_moves() const -> mcts::VectorLegalActionSet<action_t> {
  mcts::VectorLegalActionSet<action_t> moves;

  if (is_chance_node()) {
    // Environment Moves: The possible die faces
    for (int i = 1; i <= 6; ++i) {
      moves.actions.push_back(i);
    }
  } else {
    // Player Moves: Roll or Hold
    moves.actions.push_back(ACT_ROLL);
    // Can only hold if we have accumulated points (standard rule variant)
    // or if we just want to force progress.
    // Standard Pig: You can hold at 0, but it does nothing.
    // Let's strictly allow Hold only if turn_total > 0 to prune tree.
    if (turn_total > 0) {
      moves.actions.push_back(ACT_HOLD);
    }
  }
  return moves;
}

// 5. Apply Action (Transition Function)
auto PigGame::apply_action(int action) const -> PigGame {
  PigGame next = *this;
  next.apply_action_in_place(action);
  return next;
}

auto PigGame::is_valid_action(const action_t &action,
                              std::string &reason) const -> bool {
  if (is_chance_node()) {
    // Chance node: the action is the die-roll result (1-6).
    if (action < 1 || action > 6) {
      reason = "die roll out of range [1, 6]";
      return false;
    }
    return true;
  }
  // Decision node: Roll or Hold.
  if (action != ACT_ROLL && action != ACT_HOLD) {
    reason = "decision action must be ACT_ROLL(0) or ACT_HOLD(1)";
    return false;
  }
  return true;
}

void PigGame::apply_action_in_place(int action) {
  if (is_chance_node()) {
    // --- HANDLE CHANCE (Die Roll Result) ---
    // Action is the number rolled (1-6)
    if (action == 1) {
      // Rolled a 1: Lose turn total, switch player
      turn_total = 0;
      player_turn = 1 - player_turn;
    } else {
      // Rolled 2-6: Add to total, return control to player
      turn_total += action;
      // Check if this roll immediately wins the game (optional variant,
      // but usually you must Hold to bank). We stick to: must Hold to win.
    }
    // After the die lands, we are back to a decision (or next player's
    // decision)
    waiting_for_roll = false;

  } else {
    // --- HANDLE DECISION (Roll or Hold) ---
    if (action == ACT_ROLL) {
      // Player chooses to roll -> Transition to Chance Node
      waiting_for_roll = true;
    } else if (action == ACT_HOLD) {
      // Player chooses to hold -> Bank points
      if (player_turn == 0)
        p0_score += turn_total;
      else
        p1_score += turn_total;

      turn_total = 0;

      // If game isn't over, switch player
      if (p0_score < GOAL_SCORE && p1_score < GOAL_SCORE) {
        player_turn = 1 - player_turn;
      }
      waiting_for_roll = false;
    }
  }
}

auto PigGame::sample_chance_action(std::mt19937 &gen) const
    -> PigGame::action_t {
  assert(is_chance_node());
  std::uniform_int_distribution<int> dist(1, 6);
  return dist(gen);
}

auto PigGame::sample_action(std::mt19937 &gen) const -> PigGame::action_t {
  // Uniform over the valid moves, without materializing the move list.
  assert(!is_chance_node());
  if (turn_total > 0) {
    std::uniform_int_distribution<int> dist(ACT_ROLL, ACT_HOLD);
    return dist(gen);
  }
  return ACT_ROLL;
}

// 6. Current State Status (Ongoing, Win, Draw)
auto PigGame::current_state() const -> mcts::game_state_t {
  if (p0_score >= GOAL_SCORE) return mcts::win_t{0};
  if (p1_score >= GOAL_SCORE) return mcts::win_t{1};
  return mcts::ongoing_t{};
}

}  // namespace pig_game
