# MCTS framework (`game_mcts/cpp/mcts/`)

A header-only, concept-based (C++20) framework for turn-based games and Monte
Carlo Tree Search (MCTS). Concrete games built on it live next door:
`game_mcts/cpp/tictactoe/` and `game_mcts/cpp/pig_game/` (see
`game_mcts/cpp/README.md` for an overview). A full-size stochastic game
(Risk) is built on this framework in the
[risk-game-ai](https://github.com/geligeli/risk-game-ai) repo; references to
`risk-game-ai:cpp/risk/...` below point at paths in that repo.

## Design overview

The framework strictly separates three things:

1. **Rules** — the game class is *only* the transition function: given a
   state and an action, one successor. Chance (dice, shuffles) is part of
   the rules and lives on the game as `sample_chance_action()`.
2. **Policy** — choosing among a player's legal actions is *not* part of the
   game. It lives in an external `ActionProposer` passed to the search, so
   one game type can be searched with several different proposers (per
   experiment, per tournament entrant, per seat).
3. **Search** — `MctsRunner` stores the tree (a flat `std::vector` of nodes)
   and drives selection/expansion/rollout/backpropagation. A `RolloutPolicy`
   carries its own (possibly cheaper) proposer for playouts, so the tree
   policy and the playout policy can differ.

All contracts are C++20 concepts checked at compile time — there is no
abstract base class and no virtual dispatch anywhere on the hot path.

## The contracts (`game_mcts/cpp/mcts/game_traits.h`)

### `Game` concept — the only mandatory contract

```cpp
template <typename T>
concept Game =
    requires(T a, const typename T::action_t &action, std::string &reason) {
      typename T::action_t;                                   // action type
      requires Action<typename T::action_t>;                  // three_way_comparable + hashable
      { a.current_player() } -> std::same_as<int>;            // who decides (0-based)
      { a.apply_action(action) } -> std::same_as<T>;          // transition: state + action -> new state
      { a.current_state() } -> std::same_as<mcts::game_state_t>;
      { a.is_valid_action(action, reason) } -> std::convertible_to<bool>;
    };
```

- `action_t` must satisfy `mcts::Action`: `std::three_way_comparable` and
  hashable (usable in `std::unordered_set/map`). Built-in integral types
  work out of the box; structs need `operator<=>` and a `std::hash`
  specialization.
- `current_state()` returns `mcts::game_state_t`, a
  `std::variant<ongoing_t, draw_t, win_t>`; `win_t` carries
  `winning_player`. Use `mcts::is_terminal(state)` to test for game over.
- `apply_action` is **unchecked** and returns a new state by value (games
  are immutable value types; copy is the idiom).
- `is_valid_action` is a referee/debug-path oracle, *not* for hot loops: on
  failure it sets `reason` to a short human-readable explanation.

### Optional refinements (picked up automatically)

- **Number of players**: declare `static constexpr std::size_t kNumPlayers`
  on the game; defaults to 2 via the `NumPlayers` trait.
- **`InPlaceGame`**: add `void apply_action_in_place(action_t)`. Playouts
  operate on a scratch state and `PlayoutStep` prefers this form — it
  avoids a full state copy per rollout step. The usual pattern is to
  implement the mutation once and define `apply_action` as
  copy + `apply_action_in_place`.
- **`ChanceGame`**: games with chance nodes (dice, shuffles) add:

  ```cpp
  bool is_chance_node() const;
  action_t sample_chance_action(std::mt19937 &gen) const;
  ```

  `sample_chance_action` must draw from the distribution the *rules* define.
  It is deliberately not pluggable: a biased sampler would make MCTS solve a
  different game. At chance nodes `current_player()` conventionally returns
  -1.

### `ActionGenerator` — how candidate moves are delivered

An `ActionGenerator` draws actions **without replacement**, one at a time:

```cpp
{ a.next(state, gen) } -> std::optional<action_t>;  // nullopt = provably exhausted
```

`next()` takes the state rather than owning a copy (the calling MCTS node
already stores it). Stock implementations:

- `VectorLegalActionSet<ACTION>` — materialized `std::vector`, drawn via
  swap-remove. Right choice for small enumerable action spaces.
- `ArrayLegalActionSet<N, ACTION>` — fixed-size variant.
- `DedupSampler<SAMPLER, G>` — adapts a sampler's `sample()` into a
  generator by rejecting repeats; see below.

### `ActionProposer` — policy, external to the game

```cpp
template <typename P, typename G>
concept ActionProposer =
    Game<G> && requires(const P &p, const G &game, std::mt19937 &gen) {
      { p.propose(game) } -> ActionGenerator<G>;      // tree expansion, without replacement
      { p.sample(game, gen) } -> std::same_as<typename G::action_t>;  // playouts, i.i.d., no allocation
    };
```

Two entry points, deliberately:

- `propose(game)` runs once per tree node; allocating is fine.
- `sample(game, gen)` is on the rollout hot path and must not allocate.

Both must agree on the distribution of a first draw.

**`DefaultProposer<G>`** covers the common case: if the game has
`valid_moves()` returning an `ActionGenerator`, `DefaultProposer` uses it for
`propose()`, and uses the game's `sample_action(gen)` for `sample()` if
present (otherwise the first draw of a fresh `valid_moves()`).

**`BoundedProposer`** (optional refinement): `p.support_size(game)` reports
exactly how many distinct actions `sample()` can return, or
`mcts::kUnknownSupport`. Build the generator with
`mcts::MakeDedupSampler(*this, state)` and the bound is picked up, turning
exhaustion from a heuristic into a proof — this is the pattern used by
`risk_game::RiskProposer` (`risk-game-ai:cpp/risk/strategies/risk_proposer.h`).

### `Policy` (game runner) and `TournamentPolicy`

- `mcts::Policy<G>`: `policy(game) -> G` — maps a state to a successor.
  Used by `RunGame`.
- `mcts::tournament::TournamentPolicy<G>`: `policy(game, gen) ->
  PolicyDecision<G>{action, successor}` — sees the RNG so MCTS policies can
  search; used by the tournament runner (`game_mcts/cpp/mcts/tournament.h`).

## The game runner (`game_mcts/cpp/mcts/game_runner.h`)

`RunGame` plays one full game with one policy per player:

```cpp
template <Game GAME_TYPE, Policy<GAME_TYPE>... POLICY_TYPE>
  requires(sizeof...(POLICY_TYPE) == GAME_TYPE::kNumPlayers)
void RunGame(POLICY_TYPE &&...policies);
```

It default-constructs the game, then loops: query `current_player()`,
dispatch to that player's policy (a fold expression over the policy tuple),
and `apply_action` the result until `is_terminal()`. This is the simplest
possible driver — useful for smoke tests and random-vs-random games. For
search-driven play you normally drive the loop yourself (see below).

## MCTS (`game_mcts/cpp/mcts/mcts.h`, implementations in `mcts.inl`)

### Pieces

- **`MctsNode<G, PROPOSER>`** — tree node for deterministic games. Stores
  the game state, its cached `current_state()`, per-player accumulated
  rollout values (`value_t = std::array<double, kNumPlayers>`; win = +1 for
  the winner, -1 for everyone else, draw = 0), visit count, the proposer's
  untried-move generator, and expanded children.
- **`MctsStochasticNode<G, PROPOSER>`** — node for `ChanceGame`s: a
  `std::variant<PlayerNode, ChanceNode>`. Player nodes expand via the
  proposer; chance nodes expand by sampling `sample_chance_action()` and
  memoizing outcomes in a map.
- **`MctsRunner<G, PROPOSER = DefaultProposer<G>, ROLLOUT_POLICY = RandomRollout<G, PROPOSER>>`**
  — owns `node_storage` (flat vector, index 0 = root), the proposer and the
  rollout policy. Key methods:
  - `OneIteration(picker, gen)` — one select → rollout → backprop cycle.
  - `best_action()` — the root child with the most visits (robust-child
    policy).
  - `select(picker)` / `rollout(game, gen)` — exposed for custom loops.
- **Node pickers** (selection + expansion strategy):
  - `MctsNodePicker<G>(gen, widening_c = 2.0, widening_alpha = 0.5)` —
    UCB1 with progressive widening: a node may add a child only while
    `children < widening_c * visits^widening_alpha`. For small action spaces
    the generator exhausts quickly and this degenerates to vanilla MCTS; for
    huge spaces it keeps widening slowly.
  - `MctsStochasticNodePicker<G>(gen, ...)` — same for decision nodes, plus
    chance-node handling via the game's own chance distribution.
- **Rollout policies**:
  - `RandomRollout<G, PROPOSER>` — the exact default: `PlayoutStep` every
    step (rules-defined sampling at chance nodes, proposer at decision
    nodes). `max_steps` caps the playout; exceeding it scores a draw
    (`kUnlimitedRolloutSteps` = no cap).
  - `ShortcutRollout<G, SHORTCUT, PROPOSER>` — a `MoveShortcut` may return a
    successor state (cheap approximation, e.g. resolving battles with an
    expectation table); otherwise falls back to a proposer-driven
    `PlayoutStep`. Build with `MakeShortcutRollout`, combine several with
    `ChainShortcuts`. Legitimate because a *rollout* may approximate — the
    tree itself always samples truthfully.

### Minimal usage (deterministic game)

```cpp
#include "game_mcts/cpp/mcts/mcts.inl"
#include "game_mcts/cpp/tictactoe/tictactoe.h"

mcts::MctsRunner<tictactoe::TicTacToe> runner(tictactoe::TicTacToe{});
std::mt19937 gen;
auto picker = mcts::MctsNodePicker<tictactoe::TicTacToe>(gen);
for (int i = 0; i < 1000; ++i) {
  runner.OneIteration(picker, gen);
}
auto action = runner.best_action();
```

### Stochastic game with a custom proposer and rollout

This is the idiom used per move in `risk-game-ai:cpp/risk/risk_mcts_selfplay.cpp`:

```cpp
using game_t = risk_game::RiskState<2>;
using proposer_t = risk_game::RiskProposer<2>;

mcts::MctsRunner<game_t, proposer_t, MyRollout> runner(
    game, proposer_t{}, MyRollout{});
auto picker = mcts::MctsStochasticNodePicker<game_t>(gen, widening_c, alpha);
for (int i = 0; i < iterations; ++i) {
  runner.OneIteration(picker, gen);
}
game = game.apply_action(runner.best_action());
```

Note that at chance nodes you do *not* search — you sample the rules:

```cpp
if (game.is_chance_node()) {
  game = game.apply_action(game.sample_chance_action(gen));
}
```

### Inspecting the tree

- `std::cerr << runner` dumps every node (visits, values, children).
- `PlotHtmlGraph(os, runner.node_storage, renderer)` writes an interactive
  HTML tree; the renderer is a game-specific `void(std::ostream&, const G&)`
  hook (e.g. print an ascii board in a `<pre>`).
- `ExportTree(runner)` (`game_mcts/cpp/mcts/mcts_export.h`) exports the full tree to a
  game-agnostic `proto::MctsTree` for offline (e.g. Python) analysis —
  requires the game to be `SerializableGame` (see below).

## Step-by-step: implementing a game from scratch

The recipe below follows `game_mcts/cpp/tictactoe/` (deterministic) and notes where
`game_mcts/cpp/pig_game/` and `risk-game-ai:cpp/risk/` diverge.

### 1. Define the state class

Create `game_mcts/cpp/mygame/mygame.h`. The class is an immutable-ish value type: the
transition returns a new state.

```cpp
#ifndef GAME_MCTS_GAME_MCTS_CPP_MYGAME_MYGAME_H
#define GAME_MCTS_GAME_MCTS_CPP_MYGAME_MYGAME_H
#include <string>
#include "game_mcts/cpp/mcts/game_traits.h"

namespace mygame {

struct MyGame {
  using action_t = int;  // must be three_way_comparable + hashable
  static constexpr std::size_t kNumPlayers = 2;  // optional; defaults to 2

  // ... state fields ...

  auto current_player() const -> int;
  auto apply_action(action_t action) const -> MyGame;
  void apply_action_in_place(action_t action);              // optional, enables InPlaceGame
  auto current_state() const -> mcts::game_state_t;
  auto valid_moves() const -> mcts::VectorLegalActionSet<action_t>;  // enables DefaultProposer
  auto is_valid_action(const action_t &action, std::string &reason) const -> bool;
  auto sample_action(std::mt19937 &gen) const -> action_t;  // optional, allocation-free playout draw
};

static_assert(mcts::Game<MyGame>);
static_assert(mcts::InPlaceGame<MyGame>);
static_assert(mcts::ActionProposer<mcts::DefaultProposer<MyGame>, MyGame>);

}  // namespace mygame
#endif
```

Guidelines:

- Keep the state small and cheap to copy — rollouts copy/mutate it
  constantly. `TicTacToe` packs the whole board into a `uint32_t`.
- Implement the mutation in `apply_action_in_place` and define
  `apply_action` as `{ MyGame next = *this; next.apply_action_in_place(action); return next; }`.
- `action_t` must model `mcts::Action` (`std::three_way_comparable` +
  hashable). For a struct action, default `operator<=>` and add a
  `std::hash` specialization (see `risk_game::RiskAction`).

### 2. Implement the transition and the outcome

In `mygame.cpp`:

- `apply_action_in_place` — mutate state, advance `current_player`.
- `current_state()` — return `mcts::win_t{winner}`, `mcts::draw_t{}` or
  `mcts::ongoing_t{}`. Wins are scored +1/-1 automatically by the runner.
- `valid_moves()` — fill a `VectorLegalActionSet` (or `ArrayLegalActionSet`).
- `is_valid_action` — pure legality check with a `reason` string; used by
  referee/debug paths, never by the search.
- `sample_action(gen)` — a uniform (or heuristic) single draw that does not
  materialize the move list. Optional but measurably faster in rollouts.

### 3. Chance nodes (only if the game has dice/shuffles)

Add, as in `game_mcts/cpp/pig_game/pig_game.h`:

```cpp
bool is_chance_node() const;
action_t sample_chance_action(std::mt19937 &gen) const;  // the rules' true distribution
static_assert(mcts::ChanceGame<MyGame>);
```

`sample_chance_action` must sample the *exact* rules-defined distribution.
Approximations belong in rollout shortcuts, never here.

### 4. (Large games) Write a custom `ActionProposer`

When `valid_moves()` enumeration is impossible or too biased, define a
proposer in the game's `strategies/` directory (pattern:
`risk-game-ai:cpp/risk/strategies/risk_proposer.h`):

```cpp
struct MyProposer {
  using game_t = MyGame;
  auto propose(const game_t &state) const -> mcts::DedupSampler<MyProposer, game_t> {
    return mcts::MakeDedupSampler(*this, state);
  }
  auto sample(const game_t &state, std::mt19937 &gen) const -> action_t { /* one draw */ }
  auto support_size(const game_t &state) const -> std::size_t {
    // exact count of distinct sample() outcomes, or mcts::kUnknownSupport
  }
};
static_assert(mcts::ActionProposer<MyProposer, MyGame>);
static_assert(mcts::BoundedProposer<MyProposer, MyGame>);
```

Keep `sample()` and `support_size()` structurally in step (same branches) —
the static machinery asserts if `support_size()` over-reports.

### 5. Write tests

`mygame_test.cpp` with googletest, mirroring `tictactoe_test.cpp`:
transition correctness, `current_state()` on known positions,
`is_valid_action` accept/reject, and a smoke test running
`MctsRunner` for a few hundred iterations.

### 6. Add the BUILD targets

`game_mcts/cpp/mygame/BUILD`, following `game_mcts/cpp/tictactoe/BUILD`:

```python
load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@rules_cc//cc:cc_test.bzl", "cc_test")

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "mygame",
    srcs = ["mygame.cpp"],
    hdrs = ["mygame.h"],
    deps = ["//game_mcts/cpp/mcts:game_traits"],
)

cc_test(
    name = "mygame_test",
    srcs = ["mygame_test.cpp"],
    deps = [
        ":mygame",
        "@googletest//:gtest_main",
    ],
)
```

Verify: `bazel test //game_mcts/cpp/mygame:mygame_test`.

### 7. (Optional) Serialization for tree export

To enable `ExportTree` and trajectory recording, define a proto
(`mygame.proto`), then specialize the trait in
`mygame_serialization.h` (pattern: `game_mcts/cpp/tictactoe/tictactoe_serialization.h`):

```cpp
namespace mcts {
template <>
struct GameSerializationTraits<mygame::MyGame> {
  static constexpr bool kEnabled = true;
  using state_proto_t = mygame::proto::MyGameState;
  using action_proto_t = mygame::proto::MyGameAction;
  static auto StateToProto(const mygame::MyGame &) -> state_proto_t;
  static auto StateFromProto(const state_proto_t &) -> mygame::MyGame;
  static auto ActionToProto(const mygame::MyGame::action_t &) -> action_proto_t;
  static auto ActionFromProto(const action_proto_t &) -> mygame::MyGame::action_t;
};
static_assert(SerializableGame<mygame::MyGame>);
}  // namespace mcts
```

BUILD additions: `proto_library`, `cc_proto_library`, and a
`mygame_serialization` library depending on `//game_mcts/cpp/mcts:serialization`.

### 8. (Optional) Python bindings

A game with serialization traits is fully drivable from serialized
state/action protos — so it can be replayed from a recording or driven live
from Python through one game-agnostic interface. `game_mcts/cpp/mcts/py_game.h` provides
`mcts::PyGame` (type-erased driving API, pybind- and proto-free:
`state_proto()`, `apply_action_proto(bytes)`, `check_action_proto(bytes)`,
chance sampling, result) and `mcts::PyGameImpl<G>` /
`mcts::MakePyGame<G>(state, seed)` for any `ProtoSerializableGame`.
`game_mcts/cpp/mcts/py_game_binding.h` binds that interface once per module
(`mcts::BindPyGameClass(m)`).

A game module is then a few lines (pattern: `risk-game-ai:cpp/risk/risk_pybind.cpp`):

```cpp
PYBIND11_MODULE(mygame_engine, m) {
  mcts::BindPyGameClass(m, "Game");
  m.def("new_game", [](std::uint32_t seed) {
    return mcts::MakePyGame<mygame::MyGame>(mygame::MyGame{}, seed);
  });
}
```

built with `pybind_extension` (deps: `:mygame_serialization`,
`//game_mcts/cpp/mcts:py_game_binding`). On the Python side, construct the game's proto
messages with the `py_proto_library` bindings and pass
`SerializeToString()`/`FromString()` across the boundary — see
`risk-game-ai:cpp/risk/risk_engine_test.py`.

MCTS is exposed the same way for search debugging and policy evaluation.
`game_mcts/cpp/mcts/py_mcts.h` provides `mcts::PyMcts` / `mcts::MakePyMcts<G>(root,
proposer, rollout_policy, widening_c, widening_alpha, seed)`:
`run(n)`/`step()`, `root_policy()` (per-edge action bytes, visits, values),
`best_action_proto()` and `export_tree()` (serialized `mcts.proto.MctsTree`,
parsed in Python with `mcts_tree_pb2`). `py_mcts_binding.h` binds it as
`BindPyMctsClass(m, "MctsSearch")`.

Per-iteration observation is **compile-time**: `MctsRunner` has a fourth
template parameter `OBSERVER` defaulting to `NullMctsObserver`, and the single
`on_iteration(runner, value)` hook in `OneIteration` is `if constexpr`-guarded
on it — production binaries instantiate the default and compile to identical
code, while the Python module instantiates `PyIterationObserver`, which
forwards `(selection path, rollout value)` to a callable set with
`search.set_observer(fn)` (`None` disables).

```python
search = risk_engine.new_mcts(state_proto=state_bytes, rollout="expected",
                              widening_c=2.0, widening_alpha=0.5, seed=1)
search.set_observer(lambda path, value: print(path, value))
search.run(1000)
for action_bytes, visits, values in search.root_policy():
    action = risk_pb2.RiskAction.FromString(action_bytes)
tree = mcts_tree_pb2.MctsTree.FromString(search.export_tree())
```

The tree-expansion policy (the game's proposer) stays in C++; only the
rollout mode (`exact` | `expected`) is selectable from Python.

## Step-by-step: tooling to run MCTS and compute a policy

### 1. A minimal "compute one move" binary

The core idiom, enough for most purposes:

```cpp
#include "game_mcts/cpp/mcts/mcts.inl"
#include "game_mcts/cpp/mygame/mygame.h"
#include "absl/flags/flag.h"
#include "absl/flags/parse.h"

ABSL_FLAG(int, iterations, 1000, "MCTS iterations");

int main(int argc, char **argv) {
  absl::ParseCommandLine(argc, argv);
  std::mt19937 gen(std::random_device{}());

  mygame::MyGame game;
  mcts::MctsRunner<mygame::MyGame> runner(game);  // DefaultProposer + RandomRollout
  auto picker = mcts::MctsNodePicker<mygame::MyGame>(gen);
  for (int i = 0; i < absl::GetFlag(FLAGS_iterations); ++i) {
    runner.OneIteration(picker, gen);
  }
  std::cout << "best action: " << runner.best_action() << "\n";
  std::cout << runner;  // full tree dump
}
```

For a chance game, use `MctsStochasticNodePicker` instead. Add a
`cc_binary` target depending on `:mygame`, `//game_mcts/cpp/mcts:mcts`,
`@abseil-cpp//absl/flags:flag` and `:parse`, then
`bazel run //game_mcts/cpp/mygame:compute_move -- --iterations=5000`.

### 2. Extracting a policy (visit-count distribution)

`best_action()` returns the most-visited root child. For a full policy
vector, read the root node's children directly from
`runner.node_storage[0]` — each `Child{action, node_index}` maps to a child
node whose `num_visits` is the policy mass. `ExportTree(runner)` serializes
exactly this (plus values) to `proto::MctsTree` for offline analysis.

### 3. A self-play loop

Drive the game loop yourself, running MCTS at decision nodes and sampling
chance nodes directly. `risk-game-ai:cpp/risk/risk_mcts_selfplay.cpp` is the reference
implementation: per-player MCTS parameter overrides via
`--player_params='iterations=400,rollout=exact;...'`, an alternate-screen
ascii renderer, move recording into `RiskTrajectory` protobufs
(`--record_games=/tmp/games`, flushed whenever the game is paused and on
game completion), and per-move timing. It is interactive:
space pauses/resumes, and while paused `s` dumps the current state as a
protobuf-text `RiskState` and `a` dumps the ascii board (both into the
working directory), `q` quits. Run it with:

```
bazel run @risk_game_ai//cpp/risk:risk_mcts_selfplay -- --iterations_per_move=100 --delay_ms=200
```

### 4. (Optional) Faster rollouts via shortcuts

If exact rollouts are slow, add a move shortcut that approximates expensive
steps (example: `risk_rollout_shortcuts.h` resolves dice battles with a
precomputed expected-outcome table instead of sampling):

```cpp
auto rollout = mcts::MakeShortcutRollout<game_t, proposer_t>(my_shortcut);
mcts::MctsRunner<game_t, proposer_t, decltype(rollout)> runner(game, {}, rollout);
```

Shortcuts may mutate in place (`MoveShortcutInPlace`) for zero-copy rollout
steps. Remember: shortcuts are for rollouts only — the tree must always use
the exact rules.

### 5. (Optional) Tournaments between policies

`game_mcts/cpp/mcts/tournament.h` runs a multithreaded round-robin with ELO ratings.
Wrap each entrant as a `TournamentPolicy` (`policy(game, gen) ->
PolicyDecision<G>` — an MCTS policy runs `MctsRunner` internally and returns
`{runner.best_action(), game.apply_action(...)}`), collect them in the
type-erased `AnyPolicy<G>`, and hand them to the runner.
`risk-game-ai:cpp/risk/risk_tournament.cpp` is the reference binary, driven by a config
file (see `risk-game-ai:cpp/risk/example_tournament.cfg`).

### 6. Benchmarking

`game_mcts/cpp/mcts/mcts_bench.cpp` (google-benchmark) times MCTS on TicTacToe;
`risk-game-ai:cpp/risk/risk_game_benchmark.cpp` does the same for Risk transitions and
rollouts. Mirror those when you need to check that a new game's
`apply_action_in_place` / `sample_action` are fast enough for useful
iteration counts.
