#ifndef GAME_MCTS_GAME_MCTS_CPP_MCTS_MCTS_EXPORT_H
#define GAME_MCTS_GAME_MCTS_CPP_MCTS_MCTS_EXPORT_H

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <vector>

#include "game_mcts/cpp/mcts/mcts.h"
#include "game_mcts/cpp/mcts/mcts_tree.pb.h"
#include "game_mcts/cpp/mcts/serialization.h"

namespace mcts {

// Exports the full search tree of |runner| to a proto for offline (e.g.
// Python) inspection. Every node of node_storage is exported in storage order
// (index 0 = root) with its visit count and per-player accumulated values;
// every expanded child edge carries the action that created it, serialized
// via the game's serialization trait into an opaque bytes field so this
// schema stays game-agnostic. Edges are sorted by child node index so the
// export is deterministic even for chance nodes (whose children live in an
// unordered_map).
template <Game GAME_STATE, ActionProposer<GAME_STATE> PROPOSER,
          typename ROLLOUT_POLICY, typename OBSERVER>
  requires SerializableGame<GAME_STATE> &&
               RolloutPolicy<ROLLOUT_POLICY, GAME_STATE>
auto ExportTree(const MctsRunner<GAME_STATE, PROPOSER, ROLLOUT_POLICY, OBSERVER>
                    &runner) -> proto::MctsTree {
  using Traits = GameSerializationTraits<GAME_STATE>;

  proto::MctsTree tree;
  tree.mutable_nodes()->Reserve(static_cast<int>(runner.node_storage.size()));

  auto add_edge = [](proto::MctsNode *proto_node, const auto &child) {
    proto::MctsEdge *edge = proto_node->add_edges();
    edge->set_child_node_index(child.node_index);
    edge->set_action(Traits::ActionToProto(child.action).SerializeAsString());
  };

  for (const auto &node : runner.node_storage) {
    using NodeType = std::remove_cvref_t<decltype(node)>;
    proto::MctsNode *proto_node = tree.add_nodes();
    proto_node->set_num_visits(static_cast<uint32_t>(node.num_visits));
    for (double value : node.total_value) {
      proto_node->add_total_value(value);
    }
    if constexpr (is_chance_game_v<GAME_STATE>) {
      std::visit(
          overloaded{
              [&](const typename NodeType::PlayerNode &player_node) {
                proto_node->set_is_chance(false);
                // Deterministic edge order: player-node children are already
                // in insertion (ascending child index) order.
                for (const auto &child : player_node.children) {
                  add_edge(proto_node, child);
                }
              },
              [&](const typename NodeType::ChanceNode &chance_node) {
                proto_node->set_is_chance(true);
                std::vector<const typename NodeType::Child *> children;
                children.reserve(chance_node.children.size());
                for (const auto &[action, child] : chance_node.children) {
                  children.push_back(&child);
                }
                std::sort(children.begin(), children.end(),
                          [](const auto *a, const auto *b) {
                            return a->node_index < b->node_index;
                          });
                for (const auto *child : children) {
                  add_edge(proto_node, *child);
                }
              },
          },
          node.node_variant);
    } else {
      proto_node->set_is_chance(false);
      for (const auto &child : node.children) {
        add_edge(proto_node, child);
      }
    }
  }
  return tree;
}

}  // namespace mcts

#endif  // GAME_MCTS_GAME_MCTS_CPP_MCTS_MCTS_EXPORT_H
