#ifndef GAME_MCTS_GAME_MCTS_CPP_MCTS_TOURNAMENT_H
#define GAME_MCTS_GAME_MCTS_CPP_MCTS_TOURNAMENT_H

// Multithreaded round-robin tournament runner with ELO ratings for game
// policies. A policy maps (game, gen) -> PolicyDecision{action, successor};
// MCTS-based policies run the MCTS idiom internally, trivial ones just sample
// a valid move.

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <mutex>
#include <numeric>
#include <random>
#include <string>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

#include "game_mcts/cpp/mcts/game_traits.h"

namespace mcts::tournament {

// What a tournament policy returns: the action it chose plus the successor
// state. Returning the action (not just the successor) is what lets the
// PlayGame step observer record full trajectories without a game-specific
// action-recovery pass.
template <mcts::Game G>
struct PolicyDecision {
  typename G::action_t action;
  G successor;
};

// Unlike mcts::Policy, a tournament policy sees the RNG: MCTS policies need
// it for the tree search, and it lets every game of a tournament run from a
// per-game seed (deterministic regardless of thread count).
template <typename T, typename G>
concept TournamentPolicy =
    mcts::Game<G> && requires(T policy, const G &game, std::mt19937 &gen) {
      { policy(game, gen) } -> std::same_as<PolicyDecision<G>>;
    };

// Type-erased holder for any TournamentPolicy, so heterogeneous policies
// (random, MCTS with different rollout types, ...) fit in one vector.
template <mcts::Game G>
struct AnyPolicy {
  struct Concept {
    virtual ~Concept() = default;
    virtual auto operator()(const G &game,
                            std::mt19937 &gen) const -> PolicyDecision<G> = 0;
  };

  template <TournamentPolicy<G> P>
  struct Model final : Concept {
    explicit Model(P policy) : policy(std::move(policy)) {}
    auto operator()(const G &game,
                    std::mt19937 &gen) const -> PolicyDecision<G> override {
      return policy(game, gen);
    }
    P policy;
  };

  template <TournamentPolicy<G> P>
  AnyPolicy(P policy)  // NOLINT: implicit by design (type erasure).
      : impl(std::make_shared<Model<P>>(std::move(policy))) {}

  auto operator()(const G &game, std::mt19937 &gen) const -> PolicyDecision<G> {
    return (*impl)(game, gen);
  }

  std::shared_ptr<const Concept> impl;
};

struct MatchRecord {
  int policy_a;
  int policy_b;
  double score_a;  // From policy A's perspective: 1.0 win, 0.5 draw, 0.0 loss.
};

// Step observer for PlayGame: called once per applied action with the state
// before the action, the deciding player (-1 for chance steps), and the
// action itself.
template <typename O, typename G>
concept StepObserver =
    mcts::Game<G> &&
    requires(O &observer, const G &state, const typename G::action_t &action) {
      observer(state, -1, action);
    };

// Default observer: accepts any game/action and does nothing, so the
// no-observer path compiles to exactly the pre-observer code.
struct NullStepObserver {
  template <mcts::Game G>
  auto operator()(const G & /*state_before*/, int /*deciding_player*/,
                  const typename G::action_t & /*action*/) const -> void {}
};

// Per-game observer factory for RunTournament: called once per task with the
// task index, returns the step observer for that game. Observers are
// per-task, so distinct games can write distinct outputs without locking.
template <typename F, typename G>
concept StepObserverFactory =
    mcts::Game<G> && requires(F &factory, std::size_t task_index) {
      { factory(task_index) } -> StepObserver<G>;
    };

struct NullStepObserverFactory {
  auto operator()(std::size_t /*task_index*/) const -> NullStepObserver {
    return {};
  }
};

// Plays one game between two policies; player0/player1 are the policies in
// seats 0 and 1. Chance nodes (stochastic games) are resolved by sampling
// directly, bypassing the policies. |observer| (default: no-op) is invoked
// after each applied action with (state_before, deciding_player, action).
// Returns the terminal state, or draw_t when |max_moves| is reached first.
template <mcts::Game G, StepObserver<G> OBSERVER = NullStepObserver>
auto PlayGame(G game, const AnyPolicy<G> *player0, const AnyPolicy<G> *player1,
              std::mt19937 &gen, int max_moves,
              OBSERVER observer = {}) -> mcts::game_state_t {
  for (int move = 0;
       !mcts::is_terminal(game.current_state()) && move < max_moves; ++move) {
    if constexpr (mcts::ChanceGame<G>) {
      if (game.is_chance_node()) {
        const typename G::action_t action = game.sample_chance_action(gen);
        G next = game.apply_action(action);
        observer(game, -1, action);
        game = std::move(next);
        continue;
      }
    }
    const int deciding_player = game.current_player();
    const AnyPolicy<G> &policy = deciding_player == 0 ? *player0 : *player1;
    PolicyDecision<G> decision = policy(game, gen);
    observer(game, deciding_player, decision.action);
    game = std::move(decision.successor);
  }
  if (mcts::is_terminal(game.current_state())) {
    return game.current_state();
  }
  return mcts::draw_t{};  // Move cap reached.
}

// A pairing of two policy indices; a plays seat 0, b plays seat 1.
struct Task {
  int a;
  int b;
};

// Every unordered pair plays |games_per_pair| games, alternating seats:
// game k of a pair seats the lower-indexed policy first when k is even.
inline auto BuildRoundRobin(int num_policies,
                            int games_per_pair) -> std::vector<Task> {
  std::vector<Task> tasks;
  for (int a = 0; a < num_policies; ++a) {
    for (int b = a + 1; b < num_policies; ++b) {
      for (int k = 0; k < games_per_pair; ++k) {
        tasks.push_back(k % 2 == 0 ? Task{.a = a, .b = b}
                                   : Task{.a = b, .b = a});
      }
    }
  }
  return tasks;
}

// Runs all tasks on |num_threads| workers pulling indices from a shared
// atomic counter. Each task gets its own std::mt19937(base_seed ^ index) and
// writes only records[index], so results are deterministic for a fixed
// base_seed regardless of the thread count and no mutex is needed.
// |observer_factory| (default: no-op) is called once per task with the task
// index and produces that game's PlayGame step observer; observers are
// per-task, so games observed from different worker threads never share
// observer state.
template <mcts::Game G, typename InitialStateFn,
          StepObserverFactory<G> OBSERVER_FACTORY = NullStepObserverFactory>
  requires std::invocable<InitialStateFn> &&
               std::same_as<std::invoke_result_t<InitialStateFn>, G>
auto RunTournament(
    const std::vector<Task> &tasks, InitialStateFn &&initial_state_fn,
    const std::vector<AnyPolicy<G>> &policies, int num_threads,
    uint32_t base_seed, int max_moves,
    OBSERVER_FACTORY observer_factory = {}) -> std::vector<MatchRecord> {
  std::vector<MatchRecord> records(tasks.size());
  std::atomic<std::size_t> next_task{0};
  const int num_workers = std::max(1, num_threads);

  auto worker = [&]() {
    for (std::size_t i = next_task.fetch_add(1, std::memory_order_relaxed);
         i < tasks.size();
         i = next_task.fetch_add(1, std::memory_order_relaxed)) {
      std::mt19937 gen(base_seed ^ static_cast<uint32_t>(i));
      const Task &task = tasks[i];
      const mcts::game_state_t state =
          PlayGame(initial_state_fn(), &policies[task.a], &policies[task.b],
                   gen, max_moves, observer_factory(i));
      double score_a;
      if (const auto *win = std::get_if<mcts::win_t>(&state)) {
        score_a = win->winning_player == 0 ? 1.0 : 0.0;
      } else {
        score_a = 0.5;  // Draw (game draw or move cap).
      }
      records[i] = MatchRecord{
          .policy_a = task.a, .policy_b = task.b, .score_a = score_a};
      // Stream each finished game, so long tournaments can be monitored (and
      // early-stopped) from the outside instead of only at the final standings.
      {
        static std::mutex print_mutex;
        const std::lock_guard<std::mutex> lock(print_mutex);
        std::printf("GAME_DONE task=%zu total=%zu a=%d b=%d score_a=%.1f\n", i,
                    tasks.size(), task.a, task.b, score_a);
        std::fflush(stdout);
      }
    }
  };

  std::vector<std::thread> threads;
  threads.reserve(num_workers - 1);
  for (int t = 0; t < num_workers - 1; ++t) {
    threads.emplace_back(worker);
  }
  worker();  // The calling thread works too.
  for (auto &thread : threads) {
    thread.join();
  }
  return records;
}

// Standard ELO: E = 1/(1+10^((Rb-Ra)/400)), R += K*(S-E), draw = 0.5.
// Iterates passes over the records, deterministically reshuffling the order
// each pass with std::mt19937(pass), until the largest rating change in a
// pass drops below 0.01 or 200 passes ran.
inline auto ComputeElo(const std::vector<MatchRecord> &records,
                       int num_policies, double k_factor = 16.0,
                       double initial = 1500.0) -> std::vector<double> {
  std::vector<double> ratings(num_policies, initial);
  std::vector<MatchRecord> shuffled = records;
  for (int pass = 0; pass < 200; ++pass) {
    std::mt19937 gen(static_cast<uint32_t>(pass));
    std::shuffle(shuffled.begin(), shuffled.end(), gen);
    double max_change = 0.0;
    for (const auto &[a, b, score_a] : shuffled) {
      const double expected =
          1.0 / (1.0 + std::pow(10.0, (ratings[b] - ratings[a]) / 400.0));
      const double change = k_factor * (score_a - expected);
      ratings[a] += change;
      ratings[b] -= change;
      max_change = std::max(max_change, std::abs(change));
    }
    if (max_change < 0.01) {
      break;
    }
  }
  return ratings;
}

// Prints the standings sorted by rating (desc) with W/D/L counts per policy.
inline auto PrintStandings(const std::vector<double> &ratings,
                           const std::vector<std::string> &names,
                           const std::vector<MatchRecord> &records) -> void {
  std::vector<int> wins(names.size(), 0);
  std::vector<int> draws(names.size(), 0);
  std::vector<int> losses(names.size(), 0);
  for (const auto &record : records) {
    if (record.score_a == 1.0) {
      ++wins[record.policy_a];
      ++losses[record.policy_b];
    } else if (record.score_a == 0.0) {
      ++losses[record.policy_a];
      ++wins[record.policy_b];
    } else {
      ++draws[record.policy_a];
      ++draws[record.policy_b];
    }
  }

  std::vector<int> order(names.size());
  std::iota(order.begin(), order.end(), 0);
  std::sort(order.begin(), order.end(),
            [&](int x, int y) { return ratings[x] > ratings[y]; });

  printf("%-4s  %-20s  %8s  %5s  %5s  %5s\n", "Rank", "Policy", "Rating", "W",
         "D", "L");
  int rank = 1;
  for (const int i : order) {
    printf("%-4d  %-20s  %8.1f  %5d  %5d  %5d\n", rank++, names[i].c_str(),
           ratings[i], wins[i], draws[i], losses[i]);
  }
}

}  // namespace mcts::tournament

#endif  // GAME_MCTS_GAME_MCTS_CPP_MCTS_TOURNAMENT_H
