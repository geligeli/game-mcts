// Micro benchmarks for the MCTS computation on the Risk game, intended for
// profiling. Always build optimized:
//   bazel run -c opt //game_mcts/cpp/risk:risk_game_benchmark
// Profile a single benchmark with e.g.:
//   bazel build -c opt //game_mcts/cpp/risk:risk_game_benchmark
//   perf record --call-graph dwarf bazel-bin/cpp/risk/risk_game_benchmark
//       --benchmark_filter=BM_RiskMctsBatch/1000 --benchmark_min_time=10s
//   perf report

#include <benchmark/benchmark.h>

#include <random>

#include "game_mcts/cpp/risk/risk_game.h"
#include "game_mcts/cpp/risk/strategies/risk_proposer.h"
#include "game_mcts/cpp/risk/strategies/risk_rollout_shortcuts.h"
#include "game_mcts/cpp/mcts/mcts.inl"

namespace risk_game {
namespace {

using RiskGame2P = RiskState<2>;
template <typename GAME>
using ProposerFor = RiskProposer<GAME::kNumPlayers>;

// Plays |num_moves| uniformly random moves from the initial state, then
// resolves any pending chance node so the result is always a decision-node
// state (best_action() requires a player-node root). Fixed seed keeps the
// benchmark states reproducible across runs. Stops early if a terminal state
// is reached.
template <typename GAME>
auto MakeState(int num_moves, uint32_t seed = 1234) -> GAME {
  GAME game;
  std::mt19937 gen(seed);
  const ProposerFor<GAME> proposer;
  for (int i = 0; i < num_moves && !mcts::is_terminal(game.current_state());
       ++i) {
    mcts::PlayoutStep(game, proposer, gen);
  }
  if (game.is_chance_node()) {
    mcts::PlayoutStep(game, proposer, gen);
  }
  return game;
}

// First state at a chance node (attack dice roll pending).
template <typename GAME>
auto MakeChanceState(uint32_t seed = 1234) -> GAME {
  GAME game;
  std::mt19937 gen(seed);
  const ProposerFor<GAME> proposer;
  while (!game.is_chance_node()) {
    mcts::PlayoutStep(game, proposer, gen);
  }
  return game;
}

}  // namespace

// Steady-state cost of one MCTS iteration (selection + expansion + rollout +
// backpropagation) on a persistent, growing tree. Argument is the number of
// random moves played to reach the root state: 0 = initial placement,
// 100 = early game. The rollout policy is selectable: exact random rollouts
// or expected-battle shortcuts.
template <typename GAME, typename ROLLOUT_POLICY>
static void RunMctsOneIteration(benchmark::State &state,
                                const ROLLOUT_POLICY &rollout_policy) {
  const GAME game = MakeState<GAME>(static_cast<int>(state.range(0)));
  std::mt19937 gen(42);
  mcts::MctsRunner<GAME, ProposerFor<GAME>, ROLLOUT_POLICY> runner(
      game, ProposerFor<GAME>{}, rollout_policy);
  auto picker = mcts::MctsStochasticNodePicker<GAME>(gen);
  for (auto _ : state) {
    runner.OneIteration(picker, gen);
  }
  state.counters["iterations_per_second"] =
      benchmark::Counter(state.iterations(), benchmark::Counter::kIsRate);
}
static void BM_RiskMctsOneIteration_Exact(benchmark::State &state) {
  // Capped: with saturated unit counts, exact random rollouts are not
  // guaranteed to terminate; over-cap games score as draws.
  RunMctsOneIteration<RiskGame2P>(
      state,
      mcts::RandomRollout<RiskGame2P, RiskProposer<2>>{.max_steps = 50000});
}
static void BM_RiskMctsOneIteration_ExpectedBattles(benchmark::State &state) {
  RunMctsOneIteration<RiskGame2P>(
      state, mcts::MakeShortcutRollout<RiskGame2P, RiskProposer<2>>(
                 &ResolveBattleWithExpectationInPlace<2>));
}
// 3-player variant to quantify multiplayer MCTS cost.
static void BM_RiskMctsOneIteration_ExpectedBattles_3P(
    benchmark::State &state) {
  RunMctsOneIteration<RiskState<3>>(
      state, mcts::MakeShortcutRollout<RiskState<3>, RiskProposer<3>>(
                 &ResolveBattleWithExpectationInPlace<3>));
}
BENCHMARK(BM_RiskMctsOneIteration_Exact)->Arg(0)->Arg(100);
BENCHMARK(BM_RiskMctsOneIteration_ExpectedBattles)->Arg(0)->Arg(100);
BENCHMARK(BM_RiskMctsOneIteration_ExpectedBattles_3P)->Arg(0)->Arg(100);

// Full MCTS batch of N iterations starting from a fresh tree, as used per
// decision node in self-play. One batch per benchmark iteration keeps memory
// bounded. Argument: iterations per batch.
template <typename GAME, typename ROLLOUT_POLICY>
static void RunMctsBatch(benchmark::State &state,
                         const ROLLOUT_POLICY &rollout_policy) {
  const GAME game = MakeState<GAME>(100);
  std::mt19937 gen(42);
  for (auto _ : state) {
    mcts::MctsRunner<GAME, ProposerFor<GAME>, ROLLOUT_POLICY> runner(
        game, ProposerFor<GAME>{}, rollout_policy);
    auto picker = mcts::MctsStochasticNodePicker<GAME>(gen);
    for (int i = 0; i < state.range(0); ++i) {
      runner.OneIteration(picker, gen);
    }
    benchmark::DoNotOptimize(runner.best_action());
  }
  state.counters["batches_per_second"] =
      benchmark::Counter(state.iterations(), benchmark::Counter::kIsRate);
  state.counters["iterations_per_second"] = benchmark::Counter(
      state.iterations() * state.range(0), benchmark::Counter::kIsRate);
}
static void BM_RiskMctsBatch_Exact(benchmark::State &state) {
  RunMctsBatch<RiskGame2P>(
      state,
      mcts::RandomRollout<RiskGame2P, RiskProposer<2>>{.max_steps = 50000});
}
static void BM_RiskMctsBatch_ExpectedBattles(benchmark::State &state) {
  RunMctsBatch<RiskGame2P>(
      state, mcts::MakeShortcutRollout<RiskGame2P, RiskProposer<2>>(
                 &ResolveBattleWithExpectationInPlace<2>));
}
BENCHMARK(BM_RiskMctsBatch_Exact)->Arg(100)->Arg(1000);
BENCHMARK(BM_RiskMctsBatch_ExpectedBattles)->Arg(100)->Arg(1000);

// Component costs, for attributing time found while profiling the above.

// propose() generator construction + one deduplicated draw (expansion path).
static void BM_RiskProposeDraw(benchmark::State &state) {
  const RiskGame2P game = MakeState<RiskGame2P>(100);
  std::mt19937 gen(42);
  const RiskProposer<2> proposer;
  for (auto _ : state) {
    auto moves = proposer.propose(game);
    benchmark::DoNotOptimize(moves.next(game, gen));
  }
  state.counters["operations_per_second"] =
      benchmark::Counter(state.iterations(), benchmark::Counter::kIsRate);
}
BENCHMARK(BM_RiskProposeDraw);

// apply_action on a decision-node state (tree expansion + rollout steps).
static void BM_RiskApplyAction(benchmark::State &state) {
  const RiskGame2P game = MakeState<RiskGame2P>(100);
  std::mt19937 gen(42);
  const RiskGame2P::action_t action = RiskProposer<2>{}.sample(game, gen);
  for (auto _ : state) {
    benchmark::DoNotOptimize(game.apply_action(action));
  }
  state.counters["operations_per_second"] =
      benchmark::Counter(state.iterations(), benchmark::Counter::kIsRate);
}
BENCHMARK(BM_RiskApplyAction);

// Proposer sample() on a decision-node state (playout hot path).
static void BM_RiskProposerSample(benchmark::State &state) {
  const RiskGame2P game = MakeState<RiskGame2P>(100);
  std::mt19937 gen(42);
  const RiskProposer<2> proposer;
  for (auto _ : state) {
    benchmark::DoNotOptimize(proposer.sample(game, gen));
  }
  state.counters["operations_per_second"] =
      benchmark::Counter(state.iterations(), benchmark::Counter::kIsRate);
}
BENCHMARK(BM_RiskProposerSample);

// Chance-node resolution (dice roll) in rollouts.
static void BM_RiskSampleChanceAction(benchmark::State &state) {
  const RiskGame2P game = MakeChanceState<RiskGame2P>();
  std::mt19937 gen(42);
  for (auto _ : state) {
    benchmark::DoNotOptimize(game.apply_action(game.sample_chance_action(gen)));
  }
  state.counters["operations_per_second"] =
      benchmark::Counter(state.iterations(), benchmark::Counter::kIsRate);
}
BENCHMARK(BM_RiskSampleChanceAction);

}  // namespace risk_game

BENCHMARK_MAIN();
