#ifndef GAME_MCTS_GAME_MCTS_CORE_UTIL_OVERLOADED_H
#define GAME_MCTS_GAME_MCTS_CORE_UTIL_OVERLOADED_H
template <class... Ts>
struct overloaded : Ts... {
  using Ts::operator()...;
};

#endif  // GAME_MCTS_GAME_MCTS_CORE_UTIL_OVERLOADED_H
