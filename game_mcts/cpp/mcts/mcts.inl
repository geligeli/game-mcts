#ifndef GAME_MCTS_GAME_MCTS_CPP_MCTS_MCTS_INL
#define GAME_MCTS_GAME_MCTS_CPP_MCTS_MCTS_INL

#include <algorithm>
#include <cassert>
#include <cmath>
#include <iostream>
#include <ostream>
#include <random>
#include <sstream>
#include <string>

#include "game_mcts/cpp/mcts/game_traits.h"
#include "game_mcts/cpp/mcts/mcts.h"

namespace mcts {

template <typename ARRAY>
void PrintValueVector(std::ostream &os, const ARRAY &value) {
  os << "[";
  for (size_t p = 0; p < value.size(); ++p) {
    os << (p > 0 ? ", " : "") << value[p];
  }
  os << "]";
}

template <Game GAME_STATE, ActionProposer<GAME_STATE> PROPOSER>
std::ostream &operator<<(std::ostream &os,
                         const MctsNode<GAME_STATE, PROPOSER> &node) {
  os << ": visits=" << node.num_visits << ", total_value=";
  PrintValueVector(os, node.total_value);
  os << ", children=[";
  for (const auto &child : node.children) {
    os << "(action=" << child.action << ", node_index=" << child.node_index
       << "), ";
  }
  os << "]\n";
  return os;
}

template <ChanceGame GAME_STATE, ActionProposer<GAME_STATE> PROPOSER>
std::ostream &operator<<(std::ostream &os,
                         const MctsStochasticNode<GAME_STATE, PROPOSER> &node) {
  os << "visits=" << node.num_visits << ", total_value=";
  PrintValueVector(os, node.total_value);
  os << ", children=[";

  std::visit(
      overloaded{
          [&](const typename MctsStochasticNode<GAME_STATE,
                                                PROPOSER>::PlayerNode &pn) {
            for (const auto &child : pn.children) {
              os << "(action=" << child.action << ", node_index="
                 << child.node_index
                 //  << ", value=" << child.total_value << ", " <<
                 //  child.num_visits
                 << "), ";
            }
          },
          [&](const typename MctsStochasticNode<GAME_STATE,
                                                PROPOSER>::ChanceNode &cn) {
            for (const auto &[action, child] : cn.children) {
              os << "(action=" << action << ", node_index="
                 << child.node_index
                 //  << " value=" << child.total_value << " " <<
                 //  child.num_visits
                 << "), ";
            }
          }},
      node.node_variant);
  os << "]\n";
  return os;
}

template <Game GAME_STATE, ActionProposer<GAME_STATE> PROPOSER>
auto MctsNode<GAME_STATE, PROPOSER>::DebugPrint(
    const std::vector<MctsNode<GAME_STATE, PROPOSER>> &node_storage) const
    -> void {
  std::ostringstream header;
  header << "MctsNode(visits=" << num_visits << ", total_value=";
  PrintValueVector(header, total_value);
  header << ", children=[";
  LOG(INFO) << header.str();
  // Use the deciding player's component for the scalar UCB display.
  const int deciding_player = game_state.current_player();
  for (const auto &child : children) {
    const MctsNode<GAME_STATE, PROPOSER> &child_node =
        node_storage[child.node_index];
    LOG(INFO) << "  (action=" << child.action
              << ", num_visits=" << child_node.num_visits
              << " value=" << child_node.total_value[deciding_player] << " ucb="
              << ucb_value(child_node.total_value[deciding_player],
                           child_node.num_visits, num_visits)
              << " node_index=" << child.node_index << ")";
  }
  LOG(INFO) << "])";
}

auto compute_ucb(const auto &node, float two_log_parents_visits,
                 int parent_node_player, float exploration_c) -> float {
  return std::visit(
      overloaded{[&](const win_t &w) -> float {
                   // Child node terminates the game, need to pick highest score
                   // if the win is for the current player, otherwise lowest
                   return w.winning_player == parent_node_player
                              ? std::numeric_limits<float>::infinity()
                              : -std::numeric_limits<float>::infinity();
                 },
                 [](const draw_t &) -> float { return 0.0f; },
                 [&](const ongoing_t &) -> float {
                   // Multiplayer UCB: use the value component of the player who
                   // decides at the parent node (each player maximizes their
                   // own expected value).
                   const float expected_value =
                       static_cast<float>(
                           node.total_value[parent_node_player]) /
                       node.num_visits;
                   return expected_value +
                          std::sqrt(exploration_c * two_log_parents_visits /
                                    node.num_visits);
                 }},
      node.current_game_state);
};

// Base MctsNode constructor. The proposer is used once, here, to build the
// node's candidate generator; the node does not retain it.
template <Game GAME_STATE, ActionProposer<GAME_STATE> PROPOSER>
MctsNode<GAME_STATE, PROPOSER>::MctsNode(GAME_STATE state,
                                         const PROPOSER &proposer)
    : total_value{},
      num_visits(0),
      game_state(std::move(state)),
      current_game_state(game_state.current_state()),
      untried_moves{proposer.propose(game_state)},
      children{} {}

template <ChanceGame GAME_STATE, ActionProposer<GAME_STATE> PROPOSER>
MctsStochasticNode<GAME_STATE, PROPOSER>::MctsStochasticNode(
    GAME_STATE state, const PROPOSER &proposer)
    : total_value{},
      num_visits(0),
      game_state(std::move(state)),
      current_game_state(game_state.current_state()),
      node_variant{make_node_variant(game_state, proposer)} {}

template <ChanceGame GAME_STATE, ActionProposer<GAME_STATE> PROPOSER>
auto MctsStochasticNode<GAME_STATE, PROPOSER>::DebugPrint(
    const std::vector<MctsStochasticNode<GAME_STATE, PROPOSER>> &node_storage)
    const -> void {
  std::ostringstream header;
  header << "MctsNode(visits=" << num_visits << ", total_value=";
  PrintValueVector(header, total_value);
  header << ", children=[";
  LOG(INFO) << header.str();
  // Use the deciding player's component for the scalar UCB display (chance
  // nodes have current_player() == -1; clamp for display purposes only).
  const int deciding_player = std::max(0, game_state.current_player());
  std::visit(
      overloaded{
          [&](const PlayerNode &pn) {
            for (const auto &child : pn.children) {
              const MctsStochasticNode<GAME_STATE, PROPOSER> &child_node =
                  node_storage[child.node_index];
              LOG(INFO) << "  (action=" << child.action
                        << ", num_visits=" << child_node.num_visits
                        << " value=" << child_node.total_value[deciding_player]
                        << " ucb="
                        << ucb_value(child_node.total_value[deciding_player],
                                     child_node.num_visits, num_visits)
                        << " node_index=" << child.node_index << ")";
            }
          },
          [&](const ChanceNode &cn) {
            for (const auto &[action, child] : cn.children) {
              const MctsStochasticNode<GAME_STATE, PROPOSER> &child_node =
                  node_storage[child.node_index];
              LOG(INFO) << "  (action=" << action
                        << ", num_visits=" << child_node.num_visits
                        << " value=" << child_node.total_value[deciding_player]
                        << " node_index=" << child.node_index << ")";
            }
          }},
      node_variant);
  LOG(INFO) << "])";
}

template <Game GAME_STATE, ActionProposer<GAME_STATE> PROPOSER>
auto Rollout(GAME_STATE game, const PROPOSER &proposer,
             std::uniform_random_bit_generator auto &gen,
             int max_steps) -> game_state_t {
  auto state = game.current_state();
  int steps = 0;
  while (!is_terminal(state)) {
    if (steps >= max_steps) {
      return draw_t{};
    }
    // PlayoutStep prefers the game's in-place transition when it has one: the
    // rollout state is a scratch local, so copying it per step is wasted work.
    PlayoutStep(game, proposer, gen);
    ++steps;
    state = game.current_state();
  }
  return state;
};

template <Game GAME_STATE, ActionProposer<GAME_STATE> PROPOSER,
          typename ROLLOUT_POLICY, typename OBSERVER>
  requires RolloutPolicy<ROLLOUT_POLICY, GAME_STATE>
MctsRunner<GAME_STATE, PROPOSER, ROLLOUT_POLICY, OBSERVER>::MctsRunner(
    GAME_STATE root, PROPOSER proposer, ROLLOUT_POLICY rollout_policy)
    : proposer(std::move(proposer)), rollout_policy(std::move(rollout_policy)) {
  node_storage.emplace_back(std::move(root), this->proposer);
}

template <Game GAME_STATE, ActionProposer<GAME_STATE> PROPOSER,
          typename ROLLOUT_POLICY, typename OBSERVER>
  requires RolloutPolicy<ROLLOUT_POLICY, GAME_STATE>
auto MctsRunner<GAME_STATE, PROPOSER, ROLLOUT_POLICY, OBSERVER>::rollout(
    GAME_STATE game,
    std::uniform_random_bit_generator auto &gen) const -> value_t {
  return std::visit(overloaded{
                        [&](const win_t &w) -> value_t {
                          // Winner scores +1, everyone else -1.
                          value_t value{};
                          for (double &v : value) {
                            v = -1.0;
                          }
                          value[w.winning_player] = 1.0;
                          return value;
                        },
                        [&](const auto &) -> value_t { return value_t{}; },
                    },
                    rollout_policy(std::move(game), gen));
};

template <Game GAME_STATE, ActionProposer<GAME_STATE> PROPOSER,
          typename ROLLOUT_POLICY, typename OBSERVER>
  requires RolloutPolicy<ROLLOUT_POLICY, GAME_STATE>
template <typename Picker>
auto MctsRunner<GAME_STATE, PROPOSER, ROLLOUT_POLICY, OBSERVER>::select(
    Picker &&picker) -> std::size_t {
  NodePickerResult result{path.back(), /*new_action */ false,
                          /* terminal */ false};
  do {
    result = picker(path.back(), *this);
    if (result.terminal) {
      break;
    }
    path.push_back(result.node_index);
  } while (!result.new_action);
  return result.node_index;
};

template <Game GAME_STATE, ActionProposer<GAME_STATE> PROPOSER,
          typename ROLLOUT_POLICY, typename OBSERVER>
  requires RolloutPolicy<ROLLOUT_POLICY, GAME_STATE>
template <typename Picker, typename Gen>
auto MctsRunner<GAME_STATE, PROPOSER, ROLLOUT_POLICY, OBSERVER>::OneIteration(
    Picker &&picker, Gen &gen) -> void {
  path.clear();
  path.push_back(0);
  // Selection
  std::size_t node_index = select(picker);

  // Simulation
  const value_t value = rollout(node_storage[node_index].game_state, gen);
  // Backpropagation
  for (auto index : path) {
    auto &current_node = node_storage[index];
    current_node.num_visits++;
    for (size_t p = 0; p < value.size(); ++p) {
      current_node.total_value[p] += value[p];
    }
  }
  // Observation (compiled out for the default NullMctsObserver).
  if constexpr (!std::same_as<OBSERVER, NullMctsObserver>) {
    static_assert(MctsObserver<OBSERVER, MctsRunner>);
    observer.on_iteration(*this, value);
  }
}

template <Game GAME_STATE, ActionProposer<GAME_STATE> PROPOSER,
          typename ROLLOUT_POLICY, typename OBSERVER>
  requires RolloutPolicy<ROLLOUT_POLICY, GAME_STATE>
auto MctsRunner<GAME_STATE, PROPOSER, ROLLOUT_POLICY, OBSERVER>::best_action()
    const -> typename GAME_STATE::action_t {
  const auto &root_node = node_storage[0];

  if constexpr (is_chance_game_v<GAME_STATE>) {
    const auto &player_node =
        std::get<typename NodeType::PlayerNode>(root_node.node_variant);
    assert(!player_node.children.empty());
    const auto best_child = *std::max_element(
        player_node.children.begin(), player_node.children.end(),
        [&](const typename NodeType::Child &a,
            const typename NodeType::Child &b) {
          return node_storage[a.node_index].num_visits <
                 node_storage[b.node_index].num_visits;
        });
    return best_child.action;
  } else {
    assert(!root_node.children.empty());
    const auto best_child =
        *std::max_element(root_node.children.begin(), root_node.children.end(),
                          [&](const typename NodeType::Child &a,
                              const typename NodeType::Child &b) {
                            return node_storage[a.node_index].num_visits <
                                   node_storage[b.node_index].num_visits;
                          });
    return best_child.action;
  }
}

// Progressive widening limit: widening_c * num_visits^widening_alpha. The
// default alpha = 0.5 is computed as sqrt, which is much cheaper than pow.
inline auto WideningLimit(double widening_c, double widening_alpha,
                          int num_visits) -> double {
  if (widening_alpha == 0.5) {
    return widening_c * std::sqrt(static_cast<double>(num_visits));
  }
  return widening_c * std::pow(static_cast<double>(num_visits), widening_alpha);
}

/*
 Vanilla UCB1-based MCTS node picker.

 Expansion is gated by progressive widening: a node may add a new child only
 while children < widening_c * visits^widening_alpha (or while it has no
 children yet). For small action spaces the generator exhausts quickly and
 this degenerates to vanilla MCTS; for huge spaces the node keeps widening
 slowly and otherwise exploits via UCB.
*/
template <Game GAME_STATE>
auto MctsNodePicker(std::uniform_random_bit_generator auto &gen,
                    double widening_c, double widening_alpha,
                    double exploration_c) {
  return [&gen, widening_c, widening_alpha, exploration_c](
             std::size_t node_index, auto &runner) mutable -> NodePickerResult {
    using node_t = typename std::decay_t<decltype(runner)>::NodeType;
    auto &node_storage = runner.node_storage;
    auto &current_node = node_storage[node_index];
    if (is_terminal(current_node.current_game_state)) {
      return NodePickerResult{
          .node_index = node_index, .new_action = false, .terminal = true};
    }
    const bool widening_allows =
        current_node.children.empty() ||
        static_cast<double>(current_node.children.size()) <
            WideningLimit(widening_c, widening_alpha, current_node.num_visits);
    if (!current_node.untried_exhausted && widening_allows) {
      auto action =
          current_node.untried_moves.next(current_node.game_state, gen);
      if (action.has_value()) {
        typename node_t::Child child;
        child.action = *action;
        child.node_index = node_storage.size();
        current_node.children.push_back(child);
        node_storage.emplace_back(current_node.game_state.apply_action(*action),
                                  runner.proposer);
        return NodePickerResult{.node_index = node_storage.size() - 1,
                                .new_action = true,
                                .terminal = false};
      }
      current_node.untried_exhausted = true;
    };
    assert(!current_node.children.empty());
    float best_ucb = -std::numeric_limits<float>::infinity();
    std::size_t best_child_index{};
    const float kTwoParentNumVisits = 2.0f * std::log(current_node.num_visits);
    for (const auto &child : current_node.children) {
      const auto &child_node = node_storage[child.node_index];
      float ucb_score = compute_ucb(child_node, kTwoParentNumVisits,
                                    current_node.game_state.current_player(),
                                    static_cast<float>(exploration_c));
      if (ucb_score > best_ucb) {
        best_ucb = ucb_score;
        best_child_index = child.node_index;
        if (best_ucb == std::numeric_limits<float>::infinity()) {
          break;
        }
      }
    }
    return {best_child_index, false, false};
  };
}

/*
 MCTS node picker for stochastic games.
 Handles chance nodes by sampling according to their probability distribution.
 For decision nodes, uses standard UCB1 selection with progressive widening
 (see MctsNodePicker).
*/
template <ChanceGame GAME_STATE>
auto MctsStochasticNodePicker(std::uniform_random_bit_generator auto &gen,
                              double widening_c, double widening_alpha,
                              double exploration_c) {
  return [&gen, widening_c, widening_alpha, exploration_c](
             std::size_t node_index, auto &runner) -> NodePickerResult {
    using node_t = typename std::decay_t<decltype(runner)>::NodeType;
    using child_type_t = typename node_t::Child;
    auto &node_storage = runner.node_storage;
    auto &current_node = node_storage[node_index];

    // Terminal check
    if (is_terminal(current_node.current_game_state)) {
      return NodePickerResult{
          .node_index = node_index, .new_action = false, .terminal = true};
    }

    return std::visit(
        overloaded{
            [&](typename node_t::PlayerNode &pn) -> NodePickerResult {
              const bool widening_allows =
                  pn.children.empty() ||
                  static_cast<double>(pn.children.size()) <
                      WideningLimit(widening_c, widening_alpha,
                                    current_node.num_visits);
              if (!pn.untried_exhausted && widening_allows) {
                auto action =
                    pn.untried_moves.next(current_node.game_state, gen);
                if (action.has_value()) {
                  child_type_t child;
                  child.action = *action;
                  child.node_index = node_storage.size();
                  pn.children.push_back(child);
                  node_storage.emplace_back(
                      current_node.game_state.apply_action(*action),
                      runner.proposer);
                  return NodePickerResult{.node_index = child.node_index,
                                          .new_action = true,
                                          .terminal = false};
                }
                pn.untried_exhausted = true;
              }
              assert(!pn.children.empty());
              // For decision nodes: standard UCB1 selection
              float best_ucb = -std::numeric_limits<float>::infinity();
              std::size_t best_child_index{};
              const float kTwoParentNumVisits =
                  2.0f * std::log(current_node.num_visits);
              for (const auto &child : pn.children) {
                const auto &child_node = node_storage[child.node_index];
                float ucb_score =
                    compute_ucb(child_node, kTwoParentNumVisits,
                                current_node.game_state.current_player(),
                                static_cast<float>(exploration_c));
                if (ucb_score > best_ucb) {
                  best_ucb = ucb_score;
                  best_child_index = child.node_index;
                  if (best_ucb == std::numeric_limits<float>::infinity()) {
                    break;
                  }
                }
              }
              return NodePickerResult{best_child_index, false, false};
            },
            [&](typename node_t::ChanceNode &cn) -> NodePickerResult {
              auto sampled_action =
                  current_node.game_state.sample_chance_action(gen);
              auto [it, inserted] = cn.children.try_emplace(
                  sampled_action, child_type_t{
                                      .action = sampled_action,
                                      .node_index = node_storage.size(),
                                  });
              if (inserted) {
                node_storage.emplace_back(
                    current_node.game_state.apply_action(sampled_action),
                    runner.proposer);
              }
              return NodePickerResult{.node_index = it->second.node_index,
                                      .new_action = inserted,
                                      .terminal = false};
            }},
        current_node.node_variant);
  };
}

template <Game GAME_STATE, ActionProposer<GAME_STATE> PROPOSER,
          typename ROLLOUT_POLICY, typename OBSERVER>
  requires RolloutPolicy<ROLLOUT_POLICY, GAME_STATE>
std::ostream &operator<<(
    std::ostream &os,
    const MctsRunner<GAME_STATE, PROPOSER, ROLLOUT_POLICY, OBSERVER> &runner) {
  os << "MCTS Runner:\n";
  for (std::size_t i = 0; i < runner.node_storage.size(); ++i) {
    const auto &node = runner.node_storage[i];
    os << "Node " << i << ": " << node;
  }
  return os;
}

template <typename NodeType, typename Renderer>
auto PlotHtmlGraph(std::ostream &os, const std::vector<NodeType> &node_storage,
                   Renderer &&game_state_html_renderer) -> void {
  WriteHtmlGraphPrefix(os);
  if (node_storage.empty()) {
    os << "<p class=\"empty-state\">Tree is empty.</p>\n";
  } else {
    auto render_node = [&](auto &&self, int parent_visits, std::size_t index,
                           const std::string &indent) -> void {
      const auto &node = node_storage[index];
      os << indent << "<details>\n";
      os << indent << "  <summary> State: ";
      game_state_html_renderer(os, node.game_state);
      // Display the value component of the player deciding at this node.
      const int deciding_player = std::max(0, node.game_state.current_player());
      os << " &middot; visits: " << node.num_visits << " &middot; total: ";
      PrintValueVector(os, node.total_value);
      os << " &middot; ucb_value: "
         << ucb_value(node.total_value[deciding_player], node.num_visits,
                      parent_visits);

      os << "</summary>\n";
      os << indent << "  <div class=\"node-body\">\n";

      if (is_terminal(node.current_game_state)) {
        os << indent << "    <div class=\"stat-line\"><strong>Result:</strong> "
           << GameStateToString(node.current_game_state) << "</div>\n";
      }

      if (node.children.empty()) {
        os << indent << "    <div class=\"leaf\">Leaf node</div>\n";
      } else {
        os << indent << "    <div class=\"children\">\n";
        for (const auto &child : node.children) {
          os << indent << "      <div class=\"child\">\n";
          os << indent << "        <div class=\"edge-label\">action "
             << child.action << "</div>\n";
          self(self, node.num_visits, child.node_index, indent + "        ");
          os << indent << "      </div>\n";
        }
        os << indent << "    </div>\n";
      }

      os << indent << "  </div>\n";
      os << indent << "</details>\n";
    };

    render_node(render_node, 0, 0, "  ");
  }
  WriteHtmlGraphSuffix(os);
}

template <typename NodeType>
auto PlotHtmlGraph(std::ostream &os,
                   const std::vector<NodeType> &node_storage) -> void {
  using GAME_STATE = decltype(NodeType::game_state);
  PlotHtmlGraph(os, node_storage, [](std::ostream &, const GAME_STATE &) {});
}

}  // namespace mcts

#endif