#ifndef GAME_MCTS_GAME_MCTS_CPP_PIG_GAME_PIG_GAME_H
#define GAME_MCTS_GAME_MCTS_CPP_PIG_GAME_PIG_GAME_H
#include <string>

#include "game_mcts/cpp/mcts/game_traits.h"

namespace pig_game {
// --- Pig Game Implementation ---

class PigGame {
 private:
  int p0_score = 0;
  int p1_score = 0;
  int turn_total = 0;
  int player_turn = 0;            // 0 or 1
  bool waiting_for_roll = false;  // True = Chance Node, False = Decision Node

  // Constants for Decision Actions
  static constexpr int ACT_ROLL = 0;
  static constexpr int ACT_HOLD = 1;
  static constexpr int GOAL_SCORE = 100;

 public:
  using action_t = int;
  static constexpr int kNumPlayers = 2;  // Roll or Hold
  // 1. Identify current player
  // Note: Even in a chance node, the "turn" belongs to a specific player,
  // though the environment is acting.
  int current_player() const;

  // 2. Identify if this is a stochastic node
  bool is_chance_node() const;

  // 4. Generate Valid Moves
  mcts::VectorLegalActionSet<action_t> valid_moves() const;
  // Legality oracle for referee/debug paths (not hot loops). On failure sets
  // |reason| to a short explanation; on success leaves it untouched.
  bool is_valid_action(const action_t &action, std::string &reason) const;
  // 5. Apply Action (Transition Function)
  PigGame apply_action(action_t action) const;
  // In-place variant of apply_action, for scratch-state hot loops (rollouts).
  void apply_action_in_place(action_t action);

  // 6. Current State Status (Ongoing, Win, Draw)
  mcts::game_state_t current_state() const;

  // 7. Rollout policy hooks.
  // Chance nodes: the rules-defined die distribution (see mcts::ChanceGame).
  action_t sample_chance_action(std::mt19937 &gen) const;
  // Decision nodes: the game's default policy, picked up by
  // mcts::DefaultProposer as an allocation-free alternative to valid_moves().
  action_t sample_action(std::mt19937 &gen) const;

  void SetState(int p0, int p1, int turn, int player, bool waiting);
};

// --- Verification ---
static_assert(mcts::ChanceGame<PigGame>);
// mcts::PlayoutStep prefers the in-place transition in the rollout hot loop.
static_assert(mcts::InPlaceGame<PigGame>);
static_assert(mcts::ActionProposer<mcts::DefaultProposer<PigGame>, PigGame>);

}  // namespace pig_game

#endif  // GAME_MCTS_GAME_MCTS_CPP_PIG_GAME_PIG_GAME_H
