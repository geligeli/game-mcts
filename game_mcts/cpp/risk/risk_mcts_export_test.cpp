#include <gtest/gtest.h>

#include <random>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

#include "cpp/risk/risk.pb.h"
#include "cpp/risk/risk_game.h"
#include "cpp/risk/risk_serialization.h"
#include "cpp/risk/strategies/risk_proposer.h"
#include "cpp/risk/strategies/risk_rollout_shortcuts.h"
#include "game_mcts/cpp/mcts/mcts.inl"
#include "game_mcts/cpp/mcts/mcts_export.h"
#include "game_mcts/cpp/mcts/overloaded.h"

namespace risk_game {
namespace {

using risk_game_t = RiskState<2>;
using proposer_t = RiskProposer<2>;
using traits_t = mcts::GameSerializationTraits<risk_game_t>;

auto ParseEdgeAction(const std::string &bytes) -> RiskAction {
  proto::RiskAction action_proto;
  EXPECT_TRUE(action_proto.ParseFromString(bytes));
  return traits_t::ActionFromProto(action_proto);
}

// Plays a random game forward until a "real" mid-game decision node: initial
// placement over, at least one battle resolved. Rooting MCTS there reaches
// chance nodes within a few plies (from the opening state the tree would
// need ~80 plies to leave initial placement).
auto MakeMidGameRoot(std::mt19937 &gen) -> risk_game_t {
  const proposer_t proposer{};
  risk_game_t state;
  int battles = 0;
  while (state.m_initial_placement || battles < 2 || state.is_chance_node()) {
    const RiskAction action = state.is_chance_node()
                                  ? state.sample_chance_action(gen)
                                  : proposer.sample(state, gen);
    battles += std::holds_alternative<RollDiceAction>(action) ? 1 : 0;
    state = state.apply_action(action);
  }
  return state;
}

TEST(RiskMctsExportTest, TreeExportWithChanceNodes) {
  constexpr int kIterations = 300;
  std::mt19937 gen(42);
  const risk_game_t root = MakeMidGameRoot(gen);
  ASSERT_FALSE(root.m_initial_placement);

  // Rollouts resolve battles with the expectation table (cheaper); the tree
  // itself uses exact logic.
  const auto rollout_policy =
      mcts::MakeShortcutRollout<risk_game_t, proposer_t>(
          &ResolveBattleWithExpectation<2>);
  mcts::MctsRunner<risk_game_t, proposer_t, decltype(rollout_policy)> runner(
      root, proposer_t{}, rollout_policy);
  auto picker = mcts::MctsStochasticNodePicker<risk_game_t>(gen);
  for (int i = 0; i < kIterations; ++i) {
    runner.OneIteration(picker, gen);
  }

  const mcts::proto::MctsTree tree = mcts::ExportTree(runner);

  ASSERT_EQ(tree.nodes_size(), static_cast<int>(runner.node_storage.size()));
  EXPECT_EQ(tree.nodes(0).num_visits(), kIterations);

  bool saw_chance_node = false;
  for (int i = 0; i < tree.nodes_size(); ++i) {
    const mcts::proto::MctsNode &proto_node = tree.nodes(i);
    const auto &node = runner.node_storage[static_cast<std::size_t>(i)];
    using node_t = std::remove_cvref_t<decltype(node)>;

    EXPECT_EQ(proto_node.num_visits(), node.num_visits);
    ASSERT_EQ(proto_node.total_value_size(), 2);
    for (int p = 0; p < 2; ++p) {
      EXPECT_DOUBLE_EQ(proto_node.total_value(p),
                       node.total_value[static_cast<std::size_t>(p)]);
    }

    std::visit(
        overloaded{
            [&](const typename node_t::PlayerNode &player_node) {
              EXPECT_FALSE(proto_node.is_chance());
              ASSERT_EQ(proto_node.edges_size(),
                        static_cast<int>(player_node.children.size()));
              for (int e = 0; e < proto_node.edges_size(); ++e) {
                const mcts::proto::MctsEdge &edge = proto_node.edges(e);
                const auto &child =
                    player_node.children[static_cast<std::size_t>(e)];
                EXPECT_LT(edge.child_node_index(),
                          static_cast<uint64_t>(tree.nodes_size()));
                EXPECT_EQ(edge.child_node_index(), child.node_index);
                EXPECT_EQ(ParseEdgeAction(edge.action()), child.action);
              }
            },
            [&](const typename node_t::ChanceNode &chance_node) {
              saw_chance_node = true;
              EXPECT_TRUE(proto_node.is_chance());
              ASSERT_EQ(proto_node.edges_size(),
                        static_cast<int>(chance_node.children.size()));
              std::unordered_map<std::size_t, RiskAction> expected_actions;
              for (const auto &[action, child] : chance_node.children) {
                expected_actions.emplace(child.node_index, child.action);
              }
              std::size_t previous_child_index = 0;
              for (int e = 0; e < proto_node.edges_size(); ++e) {
                const mcts::proto::MctsEdge &edge = proto_node.edges(e);
                EXPECT_LT(edge.child_node_index(),
                          static_cast<uint64_t>(tree.nodes_size()));
                // Chance-node children come from an unordered_map; the
                // export must still be in (deterministic) child-index order.
                if (e > 0) {
                  EXPECT_GT(edge.child_node_index(), previous_child_index);
                }
                previous_child_index = edge.child_node_index();
                EXPECT_EQ(ParseEdgeAction(edge.action()),
                          expected_actions.at(edge.child_node_index()));
              }
            },
        },
        node.node_variant);
  }
  EXPECT_TRUE(saw_chance_node);
}

}  // namespace
}  // namespace risk_game
