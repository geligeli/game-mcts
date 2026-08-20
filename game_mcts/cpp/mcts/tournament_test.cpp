#include "game_mcts/cpp/mcts/tournament.h"

#include <gtest/gtest.h>

#include <random>
#include <vector>

#include "game_mcts/cpp/mcts/mcts.inl"
#include "game_mcts/cpp/tictactoe/tictactoe.h"

namespace mcts::tournament {
namespace {

using game_t = tictactoe::TicTacToe;

struct RandomPolicy {
  auto operator()(const game_t &game,
                  std::mt19937 &gen) const -> PolicyDecision<game_t> {
    auto moves = game.valid_moves();
    const int action = *moves.next(game, gen);
    return {.action = action, .successor = game.apply_action(action)};
  }
};

struct MctsPolicy {
  int iterations = 50;

  auto operator()(const game_t &game,
                  std::mt19937 &gen) const -> PolicyDecision<game_t> {
    MctsRunner<game_t> runner(game);
    auto picker = MctsNodePicker<game_t>(gen);
    for (int i = 0; i < iterations; ++i) {
      runner.OneIteration(picker, gen);
    }
    const auto action = runner.best_action();
    return {.action = action, .successor = game.apply_action(action)};
  }
};

static_assert(TournamentPolicy<RandomPolicy, game_t>);
static_assert(TournamentPolicy<MctsPolicy, game_t>);
static_assert(TournamentPolicy<AnyPolicy<game_t>, game_t>);

TEST(TournamentTest, AnyPolicyHoldsHeterogeneousPolicies) {
  std::vector<AnyPolicy<game_t>> policies;
  policies.emplace_back(RandomPolicy{});
  policies.emplace_back(MctsPolicy{.iterations = 10});

  std::mt19937 gen(0);
  const game_t game;
  for (const auto &policy : policies) {
    const game_t next = policy(game, gen).successor;
    // A single move of TicTacToe never ends the game.
    EXPECT_TRUE(std::holds_alternative<mcts::ongoing_t>(next.current_state()));
  }
}

TEST(TournamentTest, PlayGameTerminates) {
  const AnyPolicy<game_t> random(RandomPolicy{});
  std::mt19937 gen(1);
  // TicTacToe always ends within 9 moves, well below the cap.
  const mcts::game_state_t state =
      PlayGame(game_t{}, &random, &random, gen, 100);
  EXPECT_TRUE(mcts::is_terminal(state));
}

TEST(TournamentTest, BuildRoundRobinAlternatesSeats) {
  const auto tasks = BuildRoundRobin(3, 4);
  EXPECT_EQ(tasks.size(), 12u);  // 3 pairs * 4 games.
  for (int a = 0; a < 3; ++a) {
    for (int b = a + 1; b < 3; ++b) {
      int a_first = 0;
      for (const auto &task : tasks) {
        if ((task.a == a && task.b == b) || (task.a == b && task.b == a)) {
          a_first += task.a == a ? 1 : 0;
        }
      }
      EXPECT_EQ(a_first, 2) << "pair (" << a << ", " << b << ")";
    }
  }
}

TEST(TournamentTest, MctsBeatsRandom) {
  std::vector<AnyPolicy<game_t>> policies;
  policies.emplace_back(MctsPolicy{.iterations = 200});
  policies.emplace_back(RandomPolicy{});

  const auto tasks = BuildRoundRobin(2, 40);
  int mcts_first = 0;
  for (const auto &task : tasks) {
    mcts_first += task.a == 0 ? 1 : 0;
  }
  EXPECT_EQ(mcts_first, 20);  // Seats balanced.

  const auto records =
      RunTournament(tasks, [] { return game_t{}; }, policies, 4, 42, 100);
  ASSERT_EQ(records.size(), tasks.size());

  const auto ratings = ComputeElo(records, 2);
  EXPECT_GT(ratings[0], ratings[1]);

  double score_mcts = 0.0;
  for (const auto &record : records) {
    score_mcts += record.policy_a == 0 ? record.score_a : 1.0 - record.score_a;
  }
  EXPECT_GT(score_mcts / records.size(), 0.7);
}

TEST(TournamentTest, DeterministicAcrossThreadCounts) {
  std::vector<AnyPolicy<game_t>> policies;
  policies.emplace_back(MctsPolicy{.iterations = 20});
  policies.emplace_back(RandomPolicy{});

  const auto tasks = BuildRoundRobin(2, 8);
  const auto single =
      RunTournament(tasks, [] { return game_t{}; }, policies, 1, 7, 100);
  const auto multi =
      RunTournament(tasks, [] { return game_t{}; }, policies, 4, 7, 100);

  ASSERT_EQ(single.size(), multi.size());
  for (std::size_t i = 0; i < single.size(); ++i) {
    EXPECT_EQ(single[i].policy_a, multi[i].policy_a);
    EXPECT_EQ(single[i].policy_b, multi[i].policy_b);
    EXPECT_EQ(single[i].score_a, multi[i].score_a) << "task " << i;
  }
}

TEST(TournamentTest, ObserverRecordsReplayableGames) {
  std::vector<AnyPolicy<game_t>> policies;
  policies.emplace_back(RandomPolicy{});
  policies.emplace_back(MctsPolicy{.iterations = 20});

  const auto tasks = BuildRoundRobin(2, 6);

  struct RecordedStep {
    int player;
    int action;
  };
  struct RecordedGame {
    game_t initial;
    std::vector<RecordedStep> steps;
  };
  // One slot per task; each worker writes only its own slot.
  std::vector<RecordedGame> recordings(tasks.size());

  auto observer_factory = [&](std::size_t task_index) {
    return [recording = &recordings[task_index]](
               const game_t &state_before, int player, const int &action) {
      if (recording->steps.empty()) {
        recording->initial = state_before;
      }
      recording->steps.push_back({.player = player, .action = action});
    };
  };

  const auto observed = RunTournament(
      tasks, [] { return game_t{}; }, policies, 4, 7, 100, observer_factory);
  // Recording must not change game outcomes: same seed, same records.
  const auto plain =
      RunTournament(tasks, [] { return game_t{}; }, policies, 4, 7, 100);
  ASSERT_EQ(observed.size(), plain.size());
  for (std::size_t i = 0; i < observed.size(); ++i) {
    EXPECT_EQ(observed[i].policy_a, plain[i].policy_a);
    EXPECT_EQ(observed[i].policy_b, plain[i].policy_b);
    EXPECT_EQ(observed[i].score_a, plain[i].score_a) << "task " << i;
  }

  // Every recorded step sequence replays exactly from the initial state:
  // each step is legal, the deciding player matches, and the final state is
  // terminal with the outcome the match recorded.
  for (std::size_t i = 0; i < tasks.size(); ++i) {
    const RecordedGame &recording = recordings[i];
    game_t replay = recording.initial;
    for (const auto &[player, action] : recording.steps) {
      EXPECT_EQ(player, replay.current_player()) << "task " << i;
      std::string reason;
      EXPECT_TRUE(replay.is_valid_action(action, reason))
          << "task " << i << ": " << reason;
      replay = replay.apply_action(action);
    }
    const mcts::game_state_t final_state = replay.current_state();
    ASSERT_TRUE(mcts::is_terminal(final_state)) << "task " << i;
    double replayed_score;
    if (const auto *win = std::get_if<mcts::win_t>(&final_state)) {
      replayed_score = win->winning_player == 0 ? 1.0 : 0.0;
    } else {
      replayed_score = 0.5;
    }
    EXPECT_EQ(replayed_score, observed[i].score_a) << "task " << i;
  }
}

}  // namespace
}  // namespace mcts::tournament
