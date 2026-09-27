#include "game_mcts/core/mcts/game_traits.h"

template <class... Ts>
struct overloaded : Ts... {
  using Ts::operator()...;
};

namespace mcts {

bool is_terminal(const game_state_t &state) {
  return std::visit(overloaded{
                        [](const ongoing_t &) { return false; },
                        [](const draw_t &) { return true; },
                        [](const win_t &) { return true; },
                    },
                    state);
}

}  // namespace mcts