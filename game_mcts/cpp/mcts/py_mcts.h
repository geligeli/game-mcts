#ifndef GAME_MCTS_GAME_MCTS_CPP_MCTS_PY_MCTS_H
#define GAME_MCTS_GAME_MCTS_CPP_MCTS_PY_MCTS_H

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <random>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "game_mcts/cpp/mcts/game_traits.h"
#include "game_mcts/cpp/mcts/mcts.h"
#include "game_mcts/cpp/mcts/mcts.inl"
#include "game_mcts/cpp/mcts/mcts_export.h"
#include "game_mcts/cpp/mcts/overloaded.h"
#include "game_mcts/cpp/mcts/py_game.h"

namespace mcts {

// Type-erased MCTS search over any ProtoSerializableGame, with proto messages
// crossing as serialized bytes (the PyGame pattern). Created for a fixed root
// state; step()/run() grow the tree, root_policy() is the visit-count policy.
// The Python bindings (py_mcts_binding.h) expose this interface.
class PyMcts {
 public:
  // One expanded root edge: the action (serialized action proto), the child
  // node's visit count and its per-player accumulated values.
  struct RootAction {
    std::string action_proto;
    int visits;
    std::vector<double> total_value;
  };
  // Per-iteration observer payload: the selection path (node indices, first
  // element is the root) and the rollout value (one component per player).
  using iteration_callback_t =
      std::function<void(std::vector<std::size_t>, std::vector<double>)>;

  virtual ~PyMcts() = default;

  virtual void step() = 0;
  void run(int iterations) {
    for (int i = 0; i < iterations; ++i) {
      step();
    }
  }

  // Serialized action proto of the most-visited root child. Precondition:
  // the root is a decision node with at least one expanded child.
  virtual auto best_action_proto() const -> std::string = 0;
  // The visit-count policy over the root's expanded edges.
  virtual auto root_policy() const -> std::vector<RootAction> = 0;
  // Serialized mcts.proto.MctsTree: the full tree for offline inspection.
  virtual auto export_tree() const -> std::string = 0;
  virtual auto num_nodes() const -> int = 0;

  virtual auto state_proto_type() const -> std::string = 0;
  virtual auto action_proto_type() const -> std::string = 0;

  // Calls |callback| after every iteration (empty = off). Debugging aid; the
  // per-call copies make this unsuitable for hot loops.
  virtual void set_observer(iteration_callback_t callback) = 0;
};

// Compile-time MctsObserver for PyMcts: forwards each iteration to the
// type-erased callback. Only ever instantiated in the Python bindings;
// production runners keep the default NullMctsObserver.
struct PyIterationObserver {
  PyMcts::iteration_callback_t callback;

  template <typename RUNNER>
  void on_iteration(const RUNNER &runner,
                    const typename RUNNER::value_t &value) {
    if (callback) {
      callback(runner.path, {value.begin(), value.end()});
    }
  }
};

// PyMcts over a concrete game/proposer/rollout triple. Owns the runner, the
// picker parameters and the RNG.
template <typename G, typename PROPOSER, typename ROLLOUT_POLICY>
  requires ProtoSerializableGame<G> && ActionProposer<PROPOSER, G> &&
           RolloutPolicy<ROLLOUT_POLICY, G>
class PyMctsImpl final : public PyMcts {
 public:
  using traits_t = GameSerializationTraits<G>;
  using runner_t = MctsRunner<G, PROPOSER, ROLLOUT_POLICY, PyIterationObserver>;

  PyMctsImpl(G root, PROPOSER proposer, ROLLOUT_POLICY rollout_policy,
             double widening_c, double widening_alpha, std::uint32_t seed)
      : runner_(std::move(root), std::move(proposer),
                std::move(rollout_policy)),
        gen_(seed),
        widening_c_(widening_c),
        widening_alpha_(widening_alpha) {}

  void step() override {
    if constexpr (ChanceGame<G>) {
      auto picker =
          MctsStochasticNodePicker<G>(gen_, widening_c_, widening_alpha_);
      runner_.OneIteration(picker, gen_);
    } else {
      auto picker = MctsNodePicker<G>(gen_, widening_c_, widening_alpha_);
      runner_.OneIteration(picker, gen_);
    }
  }

  auto best_action_proto() const -> std::string override {
    return traits_t::ActionToProto(runner_.best_action()).SerializeAsString();
  }

  auto root_policy() const -> std::vector<RootAction> override {
    using node_t = typename runner_t::NodeType;
    const auto &root = runner_.node_storage[0];
    std::vector<RootAction> policy;
    const auto add_edge = [&](const typename node_t::Child &child) {
      const auto &child_node = runner_.node_storage[child.node_index];
      policy.push_back(
          {traits_t::ActionToProto(child.action).SerializeAsString(),
           child_node.num_visits,
           {child_node.total_value.begin(), child_node.total_value.end()}});
    };
    if constexpr (is_chance_game_v<G>) {
      std::visit(
          overloaded{
              [&](const typename node_t::PlayerNode &player_node) {
                for (const auto &child : player_node.children) {
                  add_edge(child);
                }
              },
              [&](const typename node_t::ChanceNode &chance_node) {
                // Deterministic order for the unordered_map children.
                std::vector<const typename node_t::Child *> children;
                children.reserve(chance_node.children.size());
                for (const auto &[action, child] : chance_node.children) {
                  children.push_back(&child);
                }
                std::sort(children.begin(), children.end(),
                          [](const auto *a, const auto *b) {
                            return a->node_index < b->node_index;
                          });
                for (const auto *child : children) {
                  add_edge(*child);
                }
              },
          },
          root.node_variant);
    } else {
      for (const auto &child : root.children) {
        add_edge(child);
      }
    }
    return policy;
  }

  auto export_tree() const -> std::string override {
    return ExportTree(runner_).SerializeAsString();
  }

  auto num_nodes() const -> int override {
    return static_cast<int>(runner_.node_storage.size());
  }

  auto state_proto_type() const -> std::string override {
    return std::string(typename traits_t::state_proto_t{}.GetTypeName());
  }
  auto action_proto_type() const -> std::string override {
    return std::string(typename traits_t::action_proto_t{}.GetTypeName());
  }

  void set_observer(iteration_callback_t callback) override {
    runner_.observer.callback = std::move(callback);
  }

 private:
  runner_t runner_;
  std::mt19937 gen_;
  double widening_c_;
  double widening_alpha_;
};

template <typename G, typename PROPOSER, typename ROLLOUT_POLICY>
  requires ProtoSerializableGame<G> && ActionProposer<PROPOSER, G> &&
               RolloutPolicy<ROLLOUT_POLICY, G>
auto MakePyMcts(G root, PROPOSER proposer, ROLLOUT_POLICY rollout_policy,
                double widening_c, double widening_alpha,
                std::uint32_t seed) -> std::unique_ptr<PyMcts> {
  return std::make_unique<PyMctsImpl<G, PROPOSER, ROLLOUT_POLICY>>(
      std::move(root), std::move(proposer), std::move(rollout_policy),
      widening_c, widening_alpha, seed);
}

// Root state from serialized proto bytes instead of a native state.
template <typename G, typename PROPOSER, typename ROLLOUT_POLICY>
  requires ProtoSerializableGame<G> && ActionProposer<PROPOSER, G> &&
               RolloutPolicy<ROLLOUT_POLICY, G>
auto MakePyMcts(const std::string &state_proto, PROPOSER proposer,
                ROLLOUT_POLICY rollout_policy, double widening_c,
                double widening_alpha,
                std::uint32_t seed) -> std::unique_ptr<PyMcts> {
  using traits_t = GameSerializationTraits<G>;
  typename traits_t::state_proto_t proto;
  if (!proto.ParseFromString(state_proto)) {
    throw std::invalid_argument("cannot parse " +
                                std::string(proto.GetTypeName()));
  }
  return MakePyMcts<G>(traits_t::StateFromProto(proto), std::move(proposer),
                       std::move(rollout_policy), widening_c, widening_alpha,
                       seed);
}

}  // namespace mcts

#endif  // GAME_MCTS_GAME_MCTS_CPP_MCTS_PY_MCTS_H
