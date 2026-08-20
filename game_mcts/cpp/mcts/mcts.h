#ifndef GAME_MCTS_GAME_MCTS_CPP_MCTS_MCTS_H
#define GAME_MCTS_GAME_MCTS_CPP_MCTS_MCTS_H

#include <array>
#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>
#include <optional>
#include <ostream>
#include <random>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "game_mcts/cpp/mcts/game_traits.h"
#include "game_mcts/cpp/mcts/overloaded.h"

namespace mcts {

auto WriteHtmlGraphSuffix(std::ostream &os) -> void;
auto WriteHtmlGraphPrefix(std::ostream &os) -> void;
auto GameStateToString(const mcts::game_state_t &state) -> std::string;
auto ucb_value(float total_value, int num_visits, int parent_visits) -> float;

// Helper to check if a game type has chance nodes at compile time.
template <typename T>
constexpr bool is_chance_game_v = ChanceGame<T>;

// Base MCTS node for deterministic games. The proposer supplies the node's
// candidate actions; it is passed at construction and its generator retained,
// so the node keeps no back-pointer to the runner that owns it.
template <Game GAME_STATE,
          ActionProposer<GAME_STATE> PROPOSER = DefaultProposer<GAME_STATE>>
struct MctsNode {
  struct Child {
    typename GAME_STATE::action_t action;
    std::size_t node_index;
    auto operator==(const Child &other) const -> bool = default;
  };

  using untried_moves_t = decltype(std::declval<const PROPOSER &>().propose(
      std::declval<const GAME_STATE &>()));
  // Accumulated rollout values, one component per player (win = +1 for the
  // winner, -1 for everyone else, draw = 0). For 2 players component 0 is
  // exactly the classic "value from the root player's perspective".
  using value_t = std::array<double, num_players_v<GAME_STATE>>;

  MctsNode(GAME_STATE state, const PROPOSER &proposer);
  auto operator==(const MctsNode<GAME_STATE, PROPOSER> &other) const
      -> bool = default;
  auto DebugPrint(const std::vector<MctsNode<GAME_STATE, PROPOSER>>
                      &node_storage) const -> void;

  value_t total_value;
  int num_visits;
  GAME_STATE game_state;
  game_state_t current_game_state;

  untried_moves_t untried_moves;
  // Set once untried_moves.next() reports exhaustion; disables further
  // expansion attempts so huge/infinite spaces stop widening the node.
  bool untried_exhausted = false;
  std::vector<Child> children;
};

template <Game GAME_STATE, ActionProposer<GAME_STATE> PROPOSER>
std::ostream &operator<<(std::ostream &os,
                         const MctsNode<GAME_STATE, PROPOSER> &node);

template <ChanceGame GAME_STATE,
          ActionProposer<GAME_STATE> PROPOSER = DefaultProposer<GAME_STATE>>
struct MctsStochasticNode {
  struct Child {
    typename GAME_STATE::action_t action;
    std::size_t node_index;
    auto operator==(const Child &other) const -> bool = default;
  };

  using untried_moves_t = decltype(std::declval<const PROPOSER &>().propose(
      std::declval<const GAME_STATE &>()));
  using value_t = std::array<double, num_players_v<GAME_STATE>>;

  MctsStochasticNode(GAME_STATE state, const PROPOSER &proposer);
  auto operator==(const MctsStochasticNode<GAME_STATE, PROPOSER> &other) const
      -> bool = default;
  auto DebugPrint(const std::vector<MctsStochasticNode<GAME_STATE, PROPOSER>>
                      &node_storage) const -> void;

  value_t total_value;
  int num_visits;
  GAME_STATE game_state;
  game_state_t current_game_state;

  struct PlayerNode {
    untried_moves_t untried_moves;
    bool untried_exhausted = false;
    std::vector<Child> children;
    auto operator==(const PlayerNode &other) const -> bool = default;
  };
  struct ChanceNode {
    std::unordered_map<typename GAME_STATE::action_t, Child> children;
    auto operator==(const ChanceNode &other) const -> bool = default;
  };
  std::variant<PlayerNode, ChanceNode> node_variant;

 private:
  static auto make_node_variant(const GAME_STATE &state,
                                const PROPOSER &proposer)
      -> std::variant<PlayerNode, ChanceNode> {
    if (state.is_chance_node()) {
      return ChanceNode{};
    } else {
      return PlayerNode{.untried_moves = proposer.propose(state),
                        .children = {}};
    }
  }
};

template <ChanceGame GAME_STATE, ActionProposer<GAME_STATE> PROPOSER>
std::ostream &operator<<(std::ostream &os,
                         const MctsStochasticNode<GAME_STATE, PROPOSER> &node);

// |exploration_c| scales the UCB1 exploration term: 1.0 is vanilla UCB1
// (sqrt(2*ln(parent_visits)/visits)); lower exploits more, higher explores.
auto compute_ucb(const auto &node, float two_log_parents_visits,
                 int parent_node_player, float exploration_c = 1.0f) -> float;

struct NodePickerResult {
  std::size_t node_index;
  bool new_action;
  bool terminal;
};

// MctsRunner's default observer: no observation. The OneIteration hook is
// `if constexpr`-guarded on this type, so runners that observe nothing
// compile to identical code with zero overhead. Python bindings instantiate
// a real observer for tree debugging (see py_mcts.h).
struct NullMctsObserver {};

// Observer concept for MctsRunner: called once per OneIteration after
// backpropagation with the runner (selection path in runner.path, tree in
// runner.node_storage) and the rollout value of that iteration.
template <typename O, typename RUNNER>
concept MctsObserver = requires(O &o, const RUNNER &runner,
                                const typename RUNNER::value_t &value) {
  { o.on_iteration(runner, value) };
};

// No step cap by default; a capped rollout returns draw_t once exceeded.
// Guards against games whose random play is not guaranteed to terminate
// (e.g. Risk rollouts with saturated mega-stacks).
inline constexpr int kUnlimitedRolloutSteps = std::numeric_limits<int>::max();

template <Game GAME_STATE, ActionProposer<GAME_STATE> PROPOSER>
auto Rollout(GAME_STATE game, const PROPOSER &proposer,
             std::uniform_random_bit_generator auto &gen,
             int max_steps = kUnlimitedRolloutSteps) -> game_state_t;

// ---------------------------------------------------------------------------
// Pluggable rollout policies
//
// The Game concept (apply_action/current_state, plus sample_chance_action for
// chance games) is the single source of exact game logic; an ActionProposer
// supplies the playout's decision-node policy. Rollouts may additionally take
// shortcuts (e.g. resolving chance nodes with expectation tables instead of
// sampling) — legitimate here precisely because it is a *rollout*
// approximation, whereas the tree must sample chance nodes truthfully.
// Three granularity levels:
//
//  - MoveShortcut: approximates a single step. Returns a successor state, or
//    nullopt to fall back to a proposer-driven PlayoutStep.
//  - MoveShortcutInPlace: same, but mutates the passed state and returns
//    whether it handled the step. Avoids copying the state per rollout step;
//    ShortcutRollout prefers this form when the shortcut supports it.
//  - RolloutPolicy: owns the whole rollout loop (allows depth caps, heuristic
//    evaluation, ...). ShortcutRollout is the stock implementation driven by
//    a MoveShortcut; RandomRollout is the exact default.
//
// Each policy carries its own proposer, so the playout policy and the tree
// expansion policy can differ (cheap sampler below, exact enumeration above).
// ---------------------------------------------------------------------------

template <typename F, typename G>
concept MoveShortcut =
    Game<G> && requires(F &f, const G &game, std::mt19937 &gen) {
      { f(game, gen) } -> std::convertible_to<std::optional<G>>;
    };

template <typename F, typename G>
concept MoveShortcutInPlace =
    Game<G> && requires(F &f, G &game, std::mt19937 &gen) {
      { f(game, gen) } -> std::convertible_to<bool>;
    };

template <typename POLICY, typename G>
concept RolloutPolicy =
    Game<G> && requires(POLICY &p, G game, std::mt19937 &gen) {
      { p(game, gen) } -> std::convertible_to<game_state_t>;
    };

// Exact rollout: every step taken by PlayoutStep (rules-defined sampling at
// chance nodes, |proposer| at decision nodes). This is the default MCTS
// behavior. max_steps caps the playout; exceeding it scores a draw.
template <Game GAME_STATE,
          ActionProposer<GAME_STATE> PROPOSER = DefaultProposer<GAME_STATE>>
struct RandomRollout {
  PROPOSER proposer{};
  int max_steps = kUnlimitedRolloutSteps;
  auto operator()(GAME_STATE game, std::uniform_random_bit_generator auto &gen)
      const -> game_state_t {
    return Rollout(std::move(game), proposer, gen, max_steps);
  }
};

// Rollout driven by a move shortcut: shortcut-provided successor when
// available, a proposer-driven PlayoutStep otherwise. Uses the in-place forms
// of both when the shortcut/game provide them, avoiding per-step copies.
template <Game GAME_STATE, typename SHORTCUT,
          ActionProposer<GAME_STATE> PROPOSER = DefaultProposer<GAME_STATE>>
  requires MoveShortcut<SHORTCUT, GAME_STATE> ||
           MoveShortcutInPlace<SHORTCUT, GAME_STATE>
struct ShortcutRollout {
  SHORTCUT shortcut;
  PROPOSER proposer{};
  int max_steps = kUnlimitedRolloutSteps;
  auto operator()(GAME_STATE game, std::uniform_random_bit_generator auto &gen)
      const -> game_state_t {
    auto state = game.current_state();
    for (int steps = 0; !is_terminal(state); ++steps) {
      if (steps >= max_steps) {
        return draw_t{};
      }
      bool advanced = false;
      if constexpr (MoveShortcutInPlace<SHORTCUT, GAME_STATE>) {
        advanced = shortcut(game, gen);
      } else if (auto next = shortcut(game, gen)) {
        game = std::move(*next);
        advanced = true;
      }
      if (!advanced) {
        PlayoutStep(game, proposer, gen);
      }
      state = game.current_state();
    }
    return state;
  }
};

template <Game GAME_STATE,
          ActionProposer<GAME_STATE> PROPOSER = DefaultProposer<GAME_STATE>,
          typename SHORTCUT>
  requires MoveShortcut<SHORTCUT, GAME_STATE> ||
               MoveShortcutInPlace<SHORTCUT, GAME_STATE>
auto MakeShortcutRollout(SHORTCUT shortcut, PROPOSER proposer = {},
                         int max_steps = kUnlimitedRolloutSteps)
    -> ShortcutRollout<GAME_STATE, SHORTCUT, PROPOSER> {
  return ShortcutRollout<GAME_STATE, SHORTCUT, PROPOSER>{
      std::move(shortcut), std::move(proposer), max_steps};
}

// Combines move shortcuts into one: the first shortcut returning a successor
// wins; if all return nullopt the rollout falls back to a PlayoutStep.
template <typename... SHORTCUTS>
auto ChainShortcuts(SHORTCUTS... shortcuts) {
  return [... shortcuts = std::move(shortcuts)](
             const auto &game,
             auto &gen) -> std::optional<std::decay_t<decltype(game)>> {
    std::optional<std::decay_t<decltype(game)>> result;
    (void)((result = shortcuts(game, gen)).has_value() || ...);
    return result;
  };
}

template <typename G, typename P>
struct GetStochasticNode {
  using type = MctsStochasticNode<G, P>;
};

template <typename G, typename P>
struct GetNormalNode {
  using type = MctsNode<G, P>;
};

// |proposer| supplies tree-expansion candidates; |rollout_policy| carries its
// own (possibly different, possibly cheaper) proposer for the playouts. When
// the rollout policy is left defaulted it gets a default-constructed proposer,
// not a copy of the one passed here — pass the rollout explicitly if it needs
// configuration.
template <Game GAME_STATE,
          ActionProposer<GAME_STATE> PROPOSER = DefaultProposer<GAME_STATE>,
          typename ROLLOUT_POLICY = RandomRollout<GAME_STATE, PROPOSER>,
          typename OBSERVER = NullMctsObserver>
  requires RolloutPolicy<ROLLOUT_POLICY, GAME_STATE>
struct MctsRunner {
  using NodeType =
      std::conditional_t<is_chance_game_v<GAME_STATE>,
                         GetStochasticNode<GAME_STATE, PROPOSER>,
                         GetNormalNode<GAME_STATE, PROPOSER>>::type;
  using value_t = typename NodeType::value_t;

  std::vector<NodeType> node_storage;
  std::vector<std::size_t> path;
  PROPOSER proposer;
  ROLLOUT_POLICY rollout_policy;
  OBSERVER observer{};

  explicit MctsRunner(GAME_STATE root, PROPOSER proposer = {},
                      ROLLOUT_POLICY rollout_policy = {});

  auto rollout(GAME_STATE game,
               std::uniform_random_bit_generator auto &gen) const -> value_t;
  template <typename Picker>
  auto select(Picker &&picker) -> std::size_t;
  template <typename Picker, typename Gen>
  auto OneIteration(Picker &&picker, Gen &gen) -> void;
  auto best_action() const -> typename GAME_STATE::action_t;
};

/*
 Vanilla UCB1-based MCTS node picker.
 Expansion is gated by progressive widening:
 children < widening_c * visits^widening_alpha.
 The node type (and with it the proposer) is deduced from the runner passed to
 the picker; GAME_STATE only pins the concept.
*/
template <Game GAME_STATE>
auto MctsNodePicker(std::uniform_random_bit_generator auto &gen,
                    double widening_c = 2.0, double widening_alpha = 0.5,
                    double exploration_c = 1.0);

/*
 MCTS node picker for stochastic games.
 Handles chance nodes by sampling the game's own chance distribution.
*/
template <ChanceGame GAME_STATE>
auto MctsStochasticNodePicker(std::uniform_random_bit_generator auto &gen,
                              double widening_c = 2.0,
                              double widening_alpha = 0.5,
                              double exploration_c = 1.0);

template <Game GAME_STATE, ActionProposer<GAME_STATE> PROPOSER,
          typename ROLLOUT_POLICY, typename OBSERVER>
  requires RolloutPolicy<ROLLOUT_POLICY, GAME_STATE>
std::ostream &operator<<(
    std::ostream &os,
    const MctsRunner<GAME_STATE, PROPOSER, ROLLOUT_POLICY, OBSERVER> &runner);

template <Game GAME_STATE, ActionProposer<GAME_STATE> PROPOSER>
auto PlotGraphviz(
    std::ostream &os,
    const std::vector<MctsNode<GAME_STATE, PROPOSER>> &node_storage) -> void;

template <typename NodeType, typename Renderer>
auto PlotHtmlGraph(std::ostream &os, const std::vector<NodeType> &node_storage,
                   Renderer &&game_state_html_renderer) -> void;

template <typename NodeType>
auto PlotHtmlGraph(std::ostream &os, const std::vector<NodeType> &node_storage);

}  // namespace mcts

#endif  // GAME_MCTS_GAME_MCTS_CPP_MCTS_MCTS_H
