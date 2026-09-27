#include "game_mcts/games/pig/pig_game.h"

#include <cassert>

#include "game_mcts/core/mcts/game_traits.h"

namespace pig_game {

int PigGame::current_player() const { return player_turn_; }

// 2. Identify if this is a stochastic node
bool PigGame::is_chance_node() const { return waiting_for_roll_; }

void PigGame::SetState(int p0, int p1, int turn, int player, bool waiting) {
  p0_score_ = p0;
  p1_score_ = p1;
  turn_total_ = turn;
  player_turn_ = player;
  waiting_for_roll_ = waiting;
}

// 4. Generate Valid Moves
auto PigGame::valid_moves() const -> mcts::VectorLegalActionSet<action_t> {
  mcts::VectorLegalActionSet<action_t> moves;

  if (is_chance_node()) {
    // Environment Moves: The possible die faces
    for (int i = 1; i <= 6; ++i) {
      moves.actions_.push_back(i);
    }
  } else {
    // Player Moves: Roll or Hold
    moves.actions_.push_back(act_roll_);
    // Can only hold if we have accumulated points (standard rule variant)
    // or if we just want to force progress.
    // Standard Pig: You can hold at 0, but it does nothing.
    // Let's strictly allow Hold only if turn_total > 0 to prune tree.
    if (turn_total_ > 0) {
      moves.actions_.push_back(act_hold_);
    }
  }
  return moves;
}

// 5. Apply Action (Transition Function)
PigGame PigGame::apply_action(int action) const {
  PigGame next = *this;
  next.apply_action_in_place(action);
  return next;
}

bool PigGame::is_valid_action(const action_t &action,
                              std::string &reason) const {
  if (is_chance_node()) {
    // Chance node: the action is the die-roll result (1-6).
    if (action < 1 || action > 6) {
      reason = "die roll out of range [1, 6]";
      return false;
    }
    return true;
  }
  // Decision node: Roll or Hold.
  if (action != act_roll_ && action != act_hold_) {
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
      turn_total_ = 0;
      player_turn_ = 1 - player_turn_;
    } else {
      // Rolled 2-6: Add to total, return control to player
      turn_total_ += action;
      // Check if this roll immediately wins the game (optional variant,
      // but usually you must Hold to bank). We stick to: must Hold to win.
    }
    // After the die lands, we are back to a decision (or next player's
    // decision)
    waiting_for_roll_ = false;

  } else {
    // --- HANDLE DECISION (Roll or Hold) ---
    if (action == act_roll_) {
      // Player chooses to roll -> Transition to Chance Node
      waiting_for_roll_ = true;
    } else if (action == act_hold_) {
      // Player chooses to hold -> Bank points
      if (player_turn_ == 0)
        p0_score_ += turn_total_;
      else
        p1_score_ += turn_total_;

      turn_total_ = 0;

      // If game isn't over, switch player
      if (p0_score_ < goal_score_ && p1_score_ < goal_score_) {
        player_turn_ = 1 - player_turn_;
      }
      waiting_for_roll_ = false;
    }
  }
}

PigGame::action_t PigGame::sample_chance_action(std::mt19937 &gen) const {
  assert(is_chance_node());
  std::uniform_int_distribution<int> dist(1, 6);
  return dist(gen);
}

PigGame::action_t PigGame::SampleAction(std::mt19937 &gen) const {
  // Uniform over the valid moves, without materializing the move list.
  assert(!is_chance_node());
  if (turn_total_ > 0) {
    std::uniform_int_distribution<int> dist(act_roll_, act_hold_);
    return dist(gen);
  }
  return act_roll_;
}

// 6. Current State Status (Ongoing, Win, Draw)
mcts::game_state_t PigGame::current_state() const {
  if (p0_score_ >= goal_score_) return mcts::win_t{0};
  if (p1_score_ >= goal_score_) return mcts::win_t{1};
  return mcts::ongoing_t{};
}

}  // namespace pig_game
