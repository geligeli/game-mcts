#ifndef GAME_MCTS_GAME_MCTS_CPP_MCTS_MINIMAX_H
#define GAME_MCTS_GAME_MCTS_CPP_MCTS_MINIMAX_H
#include "game_mcts/cpp/mcts/game_traits.h"
#include "game_mcts/cpp/mcts/overloaded.h"

namespace minimax {

auto ComputeActionValue(mcts::Game auto game, int action,
                        int root_player) -> float {
  auto next_game = game.apply_action(action);
  return std::visit(
      overloaded{[&](const mcts::win_t &w) {
                   return (w.winning_player == root_player) ? 1.0f : -1.0f;
                 },
                 [&](const mcts::draw_t &) { return 0.0f; },
                 [&](const mcts::ongoing_t &) {
                   // Next node is ongoing, need to evaluate further
                   // If we are the root player, we want to maximize our score
                   // otherwise minimize opponent's score
                   bool next_move_is_by_root_player =
                       (next_game.current_player() == root_player);
                   float sign = next_move_is_by_root_player ? 1.0f : -1.0f;
                   float max_value = -std::numeric_limits<float>::infinity();
                   auto valid_moves = next_game.valid_moves();
                   for (int next_action : valid_moves.actions) {
                     float action_value =
                         sign * ComputeActionValue(next_game, next_action,
                                                   root_player);
                     if (action_value > max_value) {
                       max_value = action_value;
                     }
                   }
                   return sign * max_value;
                 }},
      next_game.current_state());
}

auto ComputeActionValueBinaryOutcome(mcts::Game auto game, int action,
                                     int root_player) -> int {
  auto next_game = game.apply_action(action);
  return std::visit(
      overloaded{[&](const mcts::win_t &w) {
                   return (w.winning_player == root_player) ? 1 : -1;
                 },
                 [&](const mcts::draw_t &) { return 0; },
                 [&](const mcts::ongoing_t &) {
                   // Next node is ongoing, need to evaluate further
                   // If we are the root player, we want to maximize our score
                   // otherwise minimize opponent's score
                   bool next_move_is_by_root_player =
                       (next_game.current_player() == root_player);
                   int sign = next_move_is_by_root_player ? 1 : -1;
                   // NOTE: numeric_limits<int>::infinity() is 0 (integral
                   // types have no infinity), so this must be ::min() — with
                   // -infinity() losing moves (-1) would never register.
                   int max_value = std::numeric_limits<int>::min();
                   auto valid_moves = next_game.valid_moves();
                   for (int next_action : valid_moves.actions) {
                     int action_value =
                         sign * ComputeActionValueBinaryOutcome(
                                    next_game, next_action, root_player);
                     if (action_value > max_value) {
                       max_value = action_value;
                       if (max_value == 1) {
                         break;  // No need to search further
                       }
                     }
                   }
                   return sign * max_value;
                 }},
      next_game.current_state());
}

}  // namespace minimax

#endif  // GAME_MCTS_GAME_MCTS_CPP_MCTS_MINIMAX_H
