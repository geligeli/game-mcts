#ifndef GAME_MCTS_GAME_MCTS_CPP_MCTS_GAME_RUNNER_H
#define GAME_MCTS_GAME_MCTS_CPP_MCTS_GAME_RUNNER_H
#include "game_mcts/cpp/mcts/game_traits.h"

namespace mcts {

template <Game GAME_TYPE>
auto ApplyPolicy(const GAME_TYPE &game, int player,
                 Policy<GAME_TYPE> auto &policy) -> GAME_TYPE {
  return policy(game);
}

template <Game GAME_TYPE>
auto ApplyPolicy(const GAME_TYPE &game, int player,
                 Policy<GAME_TYPE> auto &policy1,
                 Policy<GAME_TYPE> auto &...policies) -> GAME_TYPE {
  if (player == 0) {
    return policy1(game);
  } else {
    return ApplyPolicy<GAME_TYPE>(game, player - 1, policies...);
  }
}

template <Game GAME_TYPE, Policy<GAME_TYPE>... POLICY_TYPE>
  requires(sizeof...(POLICY_TYPE) == GAME_TYPE::kNumPlayers)
void RunGame(POLICY_TYPE &&...policies) {
  GAME_TYPE game;

  std::tuple<POLICY_TYPE &...> policies_tuple(policies...);
  while (!game.is_terminal()) {
    int player_id = game.current_player();
    // This is a fold expression or tuple runtime dispatch trick to call the
    // correct policy
    std::apply(
        [&](auto &&...p) {
          int current_p = 0;
          // Iterate through policies and find the one matching player_id
          ((current_p++ == player_id
                ? (game.apply_action(p.get_action(game)), void())
                : void()),
           ...);
        },
        policies_tuple);
  }
}

}  // namespace mcts

#endif  // GAME_MCTS_GAME_MCTS_CPP_MCTS_GAME_RUNNER_H
