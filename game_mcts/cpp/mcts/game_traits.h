#ifndef GAME_MCTS_GAME_MCTS_CPP_MCTS_GAME_TRAITS_H
#define GAME_MCTS_GAME_MCTS_CPP_MCTS_GAME_TRAITS_H
#include <array>
#include <cassert>
#include <compare>
#include <concepts>
#include <limits>
#include <optional>
#include <random>
#include <string>
#include <type_traits>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

#include "absl/log/log.h"

namespace mcts {

struct ongoing_t {};
struct draw_t {};
struct win_t {
  int winning_player;
};

using game_state_t = std::variant<ongoing_t, draw_t, win_t>;

bool is_terminal(const game_state_t &state);

template <typename T>
concept Hashable = requires(T a) {
  { std::hash<T>{}(a) } -> std::convertible_to<std::size_t>;
};

// Number of players of a game. Games declare it via a static constexpr
// kNumPlayers member (e.g. RiskState); defaults to 2 otherwise.
template <typename G, typename = void>
struct NumPlayers : std::integral_constant<size_t, 2> {};

template <typename G>
struct NumPlayers<G, std::void_t<decltype(G::kNumPlayers)>>
    : std::integral_constant<size_t, G::kNumPlayers> {};

template <typename G>
inline constexpr size_t num_players_v = NumPlayers<G>::value;

template <typename T>
concept Action = std::three_way_comparable<T> && Hashable<T>;

namespace detail {
// Minimal Game, used only to instantiate the generator concepts at namespace
// scope in the static_asserts below.
struct ConceptGame {
  using action_t = int;
  auto current_player() const -> int { return 0; }
  auto apply_action(const action_t &) const -> ConceptGame { return *this; }
  auto current_state() const -> game_state_t { return ongoing_t{}; }
  auto is_valid_action(const action_t &, std::string &) const -> bool {
    return true;
  }
};
}  // namespace detail

// An ActionGenerator draws actions without replacement, one at a time.
// std::nullopt signals provable exhaustion of the action space. This unifies
// small fully-enumerable spaces (which exhaust exactly) with huge spaces
// (which effectively never exhaust); the MCTS node decides when to stop
// expanding via progressive widening, not via the generator.
//
// next() takes the state it is generating for rather than owning a copy: the
// caller (an MCTS node) already stores that state, and a second copy per node
// is pure overhead. Generators that do not need it simply ignore the
// argument.
template <typename T, typename G>
concept ActionGenerator = requires(T a, const G &state, std::mt19937 &gen) {
  typename T::action_t;
  requires Action<typename T::action_t>;
  { a.next(state, gen) } -> std::same_as<std::optional<typename T::action_t>>;
};

// Backwards-compatible alias for the old name.
template <typename T, typename G>
concept ActionSet = ActionGenerator<T, G>;

// Small finite spaces: enumerate all actions, draw via swap-remove. The state
// is unused — the action list was materialized up front.
template <Action ACTION>
struct VectorLegalActionSet {
  using action_t = ACTION;
  std::vector<action_t> actions;

  template <class GameState, class Generator>
  auto next(const GameState &,
            Generator &generator) -> std::optional<action_t> {
    if (actions.empty()) {
      return std::nullopt;
    }
    std::uniform_int_distribution<std::size_t> distribution(0,
                                                            actions.size() - 1);
    std::swap(actions[distribution(generator)], actions.back());
    auto result = actions.back();
    actions.pop_back();
    return result;
  }
  auto empty() const -> bool { return actions.empty(); }
};
static_assert(ActionGenerator<VectorLegalActionSet<int>, detail::ConceptGame>);

template <std::size_t N, Action ACTION>
  requires(N > 0)
struct ArrayLegalActionSet {
  using action_t = ACTION;
  std::array<action_t, N> actions;
  std::size_t size = N;

  template <class GameState, class Generator>
  auto next(const GameState &,
            Generator &generator) -> std::optional<action_t> {
    if (size == 0) {
      return std::nullopt;
    }
    std::uniform_int_distribution<std::size_t> distribution(0, size - 1);
    std::swap(actions[distribution(generator)], actions[size - 1]);
    auto result = actions[size - 1];
    --size;
    return result;
  }
  auto empty() const -> bool { return size == 0; }
};
static_assert(
    ActionGenerator<ArrayLegalActionSet<2, int>, detail::ConceptGame>);

// ---------------------------------------------------------------------------
// Rules
//
// A Game is the transition function and nothing else. The two things that
// used to live here have moved out, because they are different in kind:
//
//  - Chance-node randomness is part of the *rules*, so it stays on the game
//    but is named for what it is (ChanceGame::sample_chance_action).
//  - Choosing among a player's legal actions is *policy*, so it moved to an
//    external ActionProposer that is passed to the search.
//
// What is left is deterministic and total: given a state and an action, one
// successor. There is no random_action(): "play a random move" is
// apply_action(proposer.sample(state, gen)), which spells out whose policy
// picked the move.
// ---------------------------------------------------------------------------

// is_valid_action is a referee/debug-path oracle, NOT for hot loops: it
// returns true iff applying |action| to this state is legal; on failure it
// sets |reason| to a short human-readable explanation, on success it leaves
// |reason| untouched. apply_action stays unchecked (existing asserts stay).
template <typename T>
concept Game =
    requires(T a, const typename T::action_t &action, std::string &reason) {
      typename T::action_t;
      requires Action<typename T::action_t>;
      { a.current_player() } -> std::same_as<int>;
      { a.apply_action(action) } -> std::same_as<T>;
      { a.current_state() } -> std::same_as<mcts::game_state_t>;
      { a.is_valid_action(action, reason) } -> std::convertible_to<bool>;
    };

// Games that can advance without copying. Playouts operate on a scratch state
// and prefer this form; PlayoutStep picks it up automatically.
template <typename T>
concept InPlaceGame =
    Game<T> && requires(T a, const typename T::action_t &action) {
      a.apply_action_in_place(action);
    };

// Games with chance nodes. sample_chance_action draws from the distribution
// the *rules* define (dice, shuffles, ...), and MCTS builds its chance-node
// children directly from it — a biased sampler here would make the search
// solve a different game, which is exactly why this is not a pluggable
// policy. Precondition: is_chance_node().
template <typename T>
concept ChanceGame = Game<T> && requires(T a, std::mt19937 &gen) {
  { a.is_chance_node() } -> std::same_as<bool>;
  { a.sample_chance_action(gen) } -> std::same_as<typename T::action_t>;
};

// ---------------------------------------------------------------------------
// Action proposal (policy, external to the game)
// ---------------------------------------------------------------------------

// Proposes candidate actions at a *decision* node. Held outside the game, so
// one game type can be searched with several different proposers (per
// experiment, per tournament entrant, per seat), and a proposer can carry
// configuration, weights, or a learned prior.
//
// Two entry points, deliberately not one:
//
//  - propose(game) -> ActionGenerator, for tree expansion. Draws WITHOUT
//    replacement so a node never gets the same child twice. Runs once per
//    node, so allocating is fine.
//  - sample(game, gen) -> action_t, for playouts. A single i.i.d. draw WITH
//    replacement, on the hot path, so it must not allocate.
//
// Both must agree on the distribution of a first draw. Do NOT hold one
// propose() generator across playout steps to save the setup: that silently
// samples without replacement, which is a different (wrong) playout.
template <typename P, typename G>
concept ActionProposer =
    Game<G> && requires(const P &p, const G &game, std::mt19937 &gen) {
      { p.propose(game) } -> ActionGenerator<G>;
      requires std::same_as<typename decltype(p.propose(game))::action_t,
                            typename G::action_t>;
      { p.sample(game, gen) } -> std::same_as<typename G::action_t>;
    };

// Optional refinement of ActionProposer: the proposer knows exactly how many
// distinct actions sample() can return from a given state, or reports
// kUnknownSupport when the space is too large to count. Deliberately NOT
// written as `ActionProposer<P, G> && ...` — that would recurse, since
// checking ActionProposer instantiates propose(), which builds the
// DedupSampler that consults this trait.
//
// Implementing it turns DedupSampler's exhaustion from a heuristic into a
// proof for the small branches, which is the real payoff: with only the
// collision budget, a node whose support is nearly drawn can roll
// kMaxConsecutiveCollisions duplicates and be marked exhausted *permanently*
// with actions it never proposed.
inline constexpr std::size_t kUnknownSupport =
    std::numeric_limits<std::size_t>::max();

template <typename P, typename G>
concept BoundedProposer = requires(const P &p, const G &game) {
  { p.support_size(game) } -> std::convertible_to<std::size_t>;
};

// Adapts a proposer's sample() into an ActionGenerator by rejecting repeats.
//
// Exhaustion is reported either exactly (once |support| distinct actions have
// been handed out, for proposers that implement BoundedProposer) or
// heuristically after kMaxConsecutiveCollisions duplicates in a row, meaning
// "expanded enough" rather than provably exhausted.
//
// Holds the proposer by value, so proposers should stay cheap to copy (one
// copy per tree node); the state is passed to next() instead of stored,
// because the node calling it already owns that state.
template <typename SAMPLER, typename G>
struct DedupSampler {
  using action_t = typename G::action_t;
  static constexpr int kMaxConsecutiveCollisions = 64;
  // Hard ceiling on one next() call, so a huge-but-finite declared support
  // cannot turn a single expansion into an unbounded loop.
  static constexpr std::size_t kMaxDrawBudget = 1u << 20;

  SAMPLER sampler;
  std::size_t support = kUnknownSupport;
  std::unordered_set<action_t> seen_actions{};

  template <class Generator>
  auto next(const G &state, Generator &generator) -> std::optional<action_t> {
    // Exact stop: everything the sampler can produce has been produced. Also
    // short-circuits deterministic branches (support == 1) after one draw,
    // instead of spending the whole collision budget rediscovering it.
    if (seen_actions.size() >= support) {
      return std::nullopt;
    }
    // With a known support we know an unseen action exists, so the budget
    // scales with it: drawing the last of S actions takes ~S draws, and a flat
    // 64 would give up early and mark the node exhausted with actions left
    // (seen 29 of 30 => an 11% chance of 64 straight duplicates). The budget
    // then only guards against a support_size() that over-reports.
    const std::size_t budget =
        support == kUnknownSupport
            ? std::size_t{kMaxConsecutiveCollisions}
            : std::min<std::size_t>(kMaxDrawBudget,
                                    kMaxConsecutiveCollisions * support);
    for (std::size_t draws = 0; draws < budget; ++draws) {
      auto action = sampler.sample(state, generator);
      if (seen_actions.insert(action).second) {
        return action;
      }
    }
    assert(support == kUnknownSupport &&
           "support_size() over-reported: it claims more distinct actions "
           "than sample() can produce");
    return std::nullopt;
  }
};

// Builds a DedupSampler, picking up support_size() when the proposer has one.
// Proposers should call this from propose() rather than aggregate-initializing
// DedupSampler, so the bound is never silently left unset.
template <typename SAMPLER, typename G>
auto MakeDedupSampler(const SAMPLER &sampler,
                      const G &state) -> DedupSampler<SAMPLER, G> {
  DedupSampler<SAMPLER, G> result{.sampler = sampler};
  if constexpr (BoundedProposer<SAMPLER, G>) {
    result.support = sampler.support_size(state);
  }
  return result;
}

// Proposer for games that enumerate their own moves via valid_moves(): the
// single obvious policy, so there is no reason to make the caller name it.
// Games may additionally provide sample_action(gen) for an allocation-free
// playout draw; otherwise the first draw of a fresh valid_moves() is used
// (identical distribution, but it materializes the move list).
template <typename G>
struct DefaultProposer {
  auto propose(const G &game) const { return game.valid_moves(); }

  template <class Generator>
  auto sample(const G &game, Generator &generator) const ->
      typename G::action_t {
    if constexpr (requires { game.sample_action(generator); }) {
      return game.sample_action(generator);
    } else {
      auto moves = game.valid_moves();
      auto action = moves.next(game, generator);
      assert(action.has_value() && "proposer asked to sample a dead end");
      return *action;
    }
  }
};

// Advances |game| by one playout step: chance nodes are resolved with the
// game's own rules-defined distribution, decision nodes with the proposer.
// This is the replacement for the old Game::random_action().
template <Game G, ActionProposer<G> P, class Generator>
void PlayoutStep(G &game, const P &proposer, Generator &generator) {
  const auto action = [&]() -> typename G::action_t {
    if constexpr (ChanceGame<G>) {
      if (game.is_chance_node()) {
        return game.sample_chance_action(generator);
      }
    }
    return proposer.sample(game, generator);
  }();
  if constexpr (InPlaceGame<G>) {
    game.apply_action_in_place(action);
  } else {
    game = game.apply_action(action);
  }
}

template <typename T, typename G>
concept Policy = Game<G> && requires(T policy, G &game) {
  { policy(game) } -> std::same_as<G>;
};

}  // namespace mcts

#endif  // GAME_MCTS_GAME_MCTS_CPP_MCTS_GAME_TRAITS_H
