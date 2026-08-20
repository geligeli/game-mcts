#include <benchmark/benchmark.h>

#include <cassert>

#include "game_mcts/cpp/mcts/mcts.inl"
#include "game_mcts/cpp/tictactoe/tictactoe.h"

namespace mcts {

static void BM_OneIteratonWithPicker(benchmark::State &state) {
  tictactoe::TicTacToe game;
  MctsRunner<tictactoe::TicTacToe> runner(game);
  std::mt19937 gen;
  auto picker = MctsNodePicker<tictactoe::TicTacToe>(gen);
  for (auto _ : state) {
    runner.OneIteration(picker, gen);
  }
}

BENCHMARK(BM_OneIteratonWithPicker);

}  // namespace mcts
BENCHMARK_MAIN();
