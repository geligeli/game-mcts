#include "game_mcts/cpp/mcts/py_mcts.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <memory>
#include <random>
#include <vector>

#include "game_mcts/cpp/mcts/mcts_tree.pb.h"
#include "game_mcts/cpp/tictactoe/tictactoe.pb.h"
#include "game_mcts/cpp/tictactoe/tictactoe_serialization.h"

namespace {

using game_t = tictactoe::TicTacToe;
using mcts::PyMcts;
using traits_t = mcts::GameSerializationTraits<game_t>;

// The default observer satisfies no observer interface: the OneIteration hook
// is compiled out for it (and with it, for all production binaries).
static_assert(
    !mcts::MctsObserver<mcts::NullMctsObserver, mcts::MctsRunner<game_t>>);

struct CountingObserver {
  int count = 0;
  std::vector<std::size_t> last_path;

  template <typename RUNNER>
  void on_iteration(const RUNNER &runner,
                    const typename RUNNER::value_t &value) {
    ++count;
    last_path = runner.path;
    EXPECT_EQ(value.size(), 2u);
  }
};

TEST(PyMctsTest, ObserverHookFiresPerIteration) {
  mcts::MctsRunner<game_t, mcts::DefaultProposer<game_t>,
                   mcts::RandomRollout<game_t>, CountingObserver>
      runner(game_t{});
  std::mt19937 gen(42);
  auto picker = mcts::MctsNodePicker<game_t>(gen);
  for (int i = 0; i < 20; ++i) {
    runner.OneIteration(picker, gen);
  }
  EXPECT_EQ(runner.observer.count, 20);
  ASSERT_FALSE(runner.observer.last_path.empty());
  EXPECT_EQ(runner.observer.last_path[0], 0u);  // path starts at the root
}

TEST(PyMctsTest, TicTacToeSearch) {
  std::unique_ptr<PyMcts> search = mcts::MakePyMcts<game_t>(
      game_t{}, mcts::DefaultProposer<game_t>{}, mcts::RandomRollout<game_t>{},
      /*widening_c=*/2.0, /*widening_alpha=*/0.5,
      /*seed=*/42);
  EXPECT_EQ(search->num_nodes(), 1);
  EXPECT_EQ(search->state_proto_type(), "tictactoe.proto.TicTacToeState");

  search->run(25);
  EXPECT_GT(search->num_nodes(), 1);

  // The root policy: visits of the root children sum to the iteration count.
  int total_visits = 0;
  for (const auto &[action_proto, visits, total_value] :
       search->root_policy()) {
    tictactoe::proto::TicTacToeAction action;
    ASSERT_TRUE(action.ParseFromString(action_proto));
    EXPECT_GE(action.cell(), 0);
    EXPECT_LE(action.cell(), 8);
    EXPECT_GT(visits, 0);
    EXPECT_EQ(total_value.size(), 2u);
    total_visits += visits;
  }
  EXPECT_EQ(total_visits, 25);

  // The best action is a legal, parseable action.
  tictactoe::proto::TicTacToeAction best;
  ASSERT_TRUE(best.ParseFromString(search->best_action_proto()));
  EXPECT_GE(best.cell(), 0);
  EXPECT_LE(best.cell(), 8);

  // The exported tree is consistent: every edge lands on a valid node and
  // carries a parseable action.
  mcts::proto::MctsTree tree;
  ASSERT_TRUE(tree.ParseFromString(search->export_tree()));
  ASSERT_EQ(tree.nodes_size(), search->num_nodes());
  EXPECT_EQ(tree.nodes(0).num_visits(), 25);
  for (const auto &node : tree.nodes()) {
    for (const auto &edge : node.edges()) {
      EXPECT_LT(edge.child_node_index(), tree.nodes_size());
      tictactoe::proto::TicTacToeAction action;
      EXPECT_TRUE(action.ParseFromString(edge.action()));
    }
  }
}

TEST(PyMctsTest, ObserverCallback) {
  std::unique_ptr<PyMcts> search = mcts::MakePyMcts<game_t>(
      game_t{}, mcts::DefaultProposer<game_t>{}, mcts::RandomRollout<game_t>{},
      2.0, 0.5, /*seed=*/7);

  int calls = 0;
  std::vector<std::size_t> last_path;
  search->set_observer(
      [&](std::vector<std::size_t> path, std::vector<double> value) {
        ++calls;
        last_path = std::move(path);
        EXPECT_EQ(value.size(), 2u);
      });
  search->run(10);
  EXPECT_EQ(calls, 10);
  ASSERT_FALSE(last_path.empty());
  EXPECT_EQ(last_path[0], 0u);

  search->set_observer({});  // off
  search->run(5);
  EXPECT_EQ(calls, 10);
}

}  // namespace
