#include "game_mcts/cpp/mcts/mcts_export.h"

#include <gtest/gtest.h>

#include <random>
#include <string>

#include "game_mcts/cpp/mcts/mcts.inl"
#include "game_mcts/cpp/mcts/serialization.h"
#include "game_mcts/cpp/tictactoe/tictactoe.h"
#include "game_mcts/cpp/tictactoe/tictactoe.pb.h"
#include "game_mcts/cpp/tictactoe/tictactoe_serialization.h"

namespace mcts {
namespace {

using traits_t = GameSerializationTraits<tictactoe::TicTacToe>;

auto ParseEdgeAction(const std::string &bytes)
    -> tictactoe::TicTacToe::action_t {
  tictactoe::proto::TicTacToeAction action_proto;
  EXPECT_TRUE(action_proto.ParseFromString(bytes));
  return traits_t::ActionFromProto(action_proto);
}

TEST(MctsExportTest, TicTacToeTreeExport) {
  constexpr int kIterations = 200;
  std::mt19937 gen(42);
  MctsRunner<tictactoe::TicTacToe> runner(tictactoe::TicTacToe{});
  auto picker = MctsNodePicker<tictactoe::TicTacToe>(gen);
  for (int i = 0; i < kIterations; ++i) {
    runner.OneIteration(picker, gen);
  }

  const proto::MctsTree tree = ExportTree(runner);

  // Every node of node_storage is exported, in storage order.
  ASSERT_EQ(tree.nodes_size(), static_cast<int>(runner.node_storage.size()));
  // The root accumulated exactly one visit per iteration.
  EXPECT_EQ(tree.nodes(0).num_visits(), kIterations);

  for (int i = 0; i < tree.nodes_size(); ++i) {
    const proto::MctsNode &proto_node = tree.nodes(i);
    const auto &node = runner.node_storage[static_cast<std::size_t>(i)];

    EXPECT_EQ(proto_node.num_visits(), node.num_visits);
    ASSERT_EQ(proto_node.total_value_size(), 2);
    for (int p = 0; p < 2; ++p) {
      EXPECT_DOUBLE_EQ(proto_node.total_value(p),
                       node.total_value[static_cast<std::size_t>(p)]);
    }
    // TicTacToe has no chance nodes.
    EXPECT_FALSE(proto_node.is_chance());

    ASSERT_EQ(proto_node.edges_size(), static_cast<int>(node.children.size()));
    std::size_t previous_child_index = 0;
    for (int e = 0; e < proto_node.edges_size(); ++e) {
      const proto::MctsEdge &edge = proto_node.edges(e);
      const auto &child = node.children[static_cast<std::size_t>(e)];
      // Child indices reference exported nodes and edges are ordered.
      EXPECT_LT(edge.child_node_index(),
                static_cast<uint64_t>(tree.nodes_size()));
      EXPECT_EQ(edge.child_node_index(), child.node_index);
      if (e > 0) {
        EXPECT_GT(edge.child_node_index(), previous_child_index);
      }
      previous_child_index = edge.child_node_index();
      // The opaque action payload parses back through the game trait and
      // matches the action stored on the edge.
      EXPECT_EQ(ParseEdgeAction(edge.action()), child.action);
    }
  }
}

TEST(MctsExportTest, TicTacToeStateRoundTrip) {
  std::mt19937 gen(7);
  tictactoe::TicTacToe state;
  while (!is_terminal(state.current_state())) {
    const auto proto = traits_t::StateToProto(state);
    const auto restored = traits_t::StateFromProto(proto);
    EXPECT_EQ(restored.board_state, state.board_state);
    EXPECT_EQ(restored.current_player(), state.current_player());
    state = state.apply_action(state.sample_action(gen));
  }
}

}  // namespace
}  // namespace mcts
