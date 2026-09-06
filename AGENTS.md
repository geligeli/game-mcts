# AGENTS.md

## What this repo is

Header-only, concept-based (C++20 concepts, C++23 code) framework for
turn-based games + Monte Carlo Tree Search, built with Bazel 8.x
(Bzlmod, see `MODULE.bazel`). No virtual dispatch on the hot path.

Strict separation, enforced by concepts in
`game_mcts/core/mcts/game_traits.h`:

- **Rules** — the game class: state + action -> successor. Chance (dice,
  shuffles) is part of the rules via `sample_chance_action()`.
- **Policy** — external `ActionProposer` choosing moves, never part of the game.
- **Search** — `MctsRunner` (tree + selection/rollout/backprop).

Dependency direction: `common` <- `core` <- `games` <- `arena`, `tools` at the
top (nothing may depend on `tools`). `tournament_server` (the arena) sits
outside that chain and depends on nothing else in this repo; `game_mcts/arena`
is the only thing that joins the two.

New to the framework? Read `game_mcts/core/mcts/README.md` (contracts +
step-by-step for a new game) and copy `game_mcts/games/tictactoe/`
(deterministic) or `game_mcts/games/pig/` (chance nodes). The full-size
example is `game_mcts/games/risk/`.

## Build / test / run

```sh
bazel build //...
bazel test //...
bazel run //game_mcts/tools/bench:mcts_bench
```

- Target a subtree while iterating: `bazel test //game_mcts/games/risk/...`,
  `bazel test //game_mcts/core/mcts:mcts_test`, etc.
- Bazel labels look like `//game_mcts/core/mcts:mcts`; headers are included
  with the full repo-relative path, e.g.
  `#include "game_mcts/core/mcts/mcts.h"`.
- `MctsRunner` implementation lives in `mcts.inl` — callers must
  `#include "game_mcts/core/mcts/mcts.inl"`, not just `mcts.h`.
- Sanitizer / tuning configs in `.bazelrc` (each gets its own output dir):
  `--config=asan`, `--config=tsan`, `--config=ubsan`, `--config=msan`,
  `--config=native` (`-march=native`, faster rollouts, non-portable binaries).
  Example: `bazel test --config=asan //game_mcts/games/risk/...`.
- Python: games expose a `risk_engine`-style pybind module plus
  `py_proto_library` targets. After C++ changes rebuild, e.g.
  `bazel build //game_mcts/games/risk:risk_engine //game_mcts/games/risk:risk_py_proto`.
  Canonical Python test example: `game_mcts/games/risk/risk_engine_test.py`.
- MCP servers in `mcp_servers/` (`risk_mcp`, `arena_mcp`) need their venv
  and generated stubs; see `mcp_servers/README.md`. Do not commit the venv
  or generated `_pb/` / `*_pb2.py` files (git-ignored).

## C++ conventions

- C++23 (`-std=c++23` in `.bazelrc`), `-Wall -Wextra -Werror` for project
  files; third-party under `external/` is silenced with `-w`, never "fix"
  warnings there.
- Format: Google style, `clang-format -i -style=google` (pre-commit hook).
  `.clang-format` sets `AlwaysBreakTemplateDeclarations: Yes`,
  `IncludeBlocks: IBS_Preserve` — keep them.
- Header guards, never `#pragma once`. Guard form is
  `GAME_MCTS_<REL_PATH_WITH_UNDERSCORES>` (repo name prefix included),
  e.g. `#ifndef GAME_MCTS_GAME_MCTS_CPP_MYGAME_MYGAME_H`. The pre-commit
  hook runs `scripts/fix_guards.py`, which rewrites `#pragma once` to a
  guard and fails the commit so you re-stage — write the guard correctly
  up front.
- Games are immutable-ish value types: implement mutation once in
  `apply_action_in_place(action)`, define `apply_action` as
  copy + in-place. Keep state small and cheap to copy.
- `action_t` must model `mcts::Action` (`three_way_comparable` + hashable);
  struct actions need `operator<=>` plus a `std::hash` specialization
  (pattern: `risk_game::RiskAction`).
- `is_valid_action(action, reason)` is a referee/debug oracle, never the hot
  path; on failure set `reason`, on success leave it untouched.
- `sample_chance_action(gen)` must draw the rules' **exact** distribution.
  Approximations belong in rollout `MoveShortcut`s, never in the game or
  the tree. At chance nodes `current_player()` returns -1 and callers
  sample directly, they do not search.
- `ActionProposer` has two entry points that must agree on first-draw
  distribution: `propose(game)` (once per tree node, may allocate) and
  `sample(game, gen)` (rollout hot path, must not allocate). Small games
  use `DefaultProposer` via `valid_moves()` (+ optional allocation-free
  `sample_action(gen)`); large games write a custom proposer returning
  `MakeDedupSampler(*this, state)` and keep `support_size()` structurally
  in step with `sample()` (pattern:
  `game_mcts/games/risk/strategies/risk_proposer.h`).
- Concepts are compile-time contracts: add `static_assert(mcts::Game<G>)`
  (plus `InPlaceGame` / `ChanceGame` / `ActionProposer` /
  `BoundedProposer` / `SerializableGame` as applicable) next to each type.
- Template picker/runner idiom: deterministic games use
  `MctsNodePicker`, chance games use `MctsStochasticNodePicker`; read the
  policy from root children visits (`best_action()` = robust child), and
  dump with `std::cerr << runner` or `PlotHtmlGraph` / `ExportTree`.
- `MctsRunner`'s 4th template param is an observer defaulting to
  `NullMctsObserver` (zero-cost when unused); only the Python bindings
  instantiate the forwarding observer.

## Bazel / proto / pybind conventions

- Follow the `game_mcts/games/tictactoe/BUILD` pattern: `cc_library` +
  `cc_test` (+ `proto_library` / `cc_proto_library` /
  `<game>_serialization` lib for tree export, `pybind_extension` for the
  engine module). Loads come from `@rules_cc//cc:cc_library.bzl` etc.
  (see existing BUILD files), not bare `cc_library`.
- `package(default_visibility = ["//visibility:public"])` in game/framework
  BUILD files.
- Serialization (`ExportTree`, trajectory recording, tournament wire
  format, Python driving) requires specializing
  `mcts::GameSerializationTraits<G>` with `kEnabled = true` and the four
  `State/ActionTo/FromProto` functions (pattern:
  `tictactoe_serialization.h`). State/action bytes on the wire and across
  the pybind boundary are always serialized protos.
- `arena/candidate_api/candidate_api.h` is header-only because the game is
  selected at compile time (`CANDIDATE_GAME_RISK2` vs
  `CANDIDATE_GAME_TICTACTOE`); candidates define only
  `MakePolicy(const candidate::Params&)`. Start from
  `game_mcts/arena/candidates/dev/strategy.h`.

## Tests

- googletest via `@googletest//:gtest_main`, one `<name>_test.cpp` +
  `cc_test` per unit (see `tictactoe_test.cpp`, `mcts_test.cpp`).
- New game tests must cover: transition correctness, `current_state()` on
  known positions, `is_valid_action` accept/reject with reason strings,
  and an `MctsRunner` smoke run (few hundred iterations).
- Verify with `bazel test //path/to:target` for the new code plus
  `bazel test //...` before finishing; use `--config=asan` (or tsan/ubsan)
  when touching game state, proposers, or rollout code.
- Benchmarks use google-benchmark (patterns: `mcts_bench.cpp`,
  `risk_game_benchmark.cpp`); tournaments use `core/mcts/tournament.h`
  with `TournamentPolicy` / `AnyPolicy` wrappers (reference:
  `risk_tournament.cpp` + `example_tournament.cfg`).

## Tournament server / arena (agents)

- Broker protocol and flags are documented in
  `game_mcts/tournament_server/README.md`; the agent submission loop in
  `game_mcts/tournament_server/ARENA.md`. A candidate id is its broker
  player name; `player:<name>` rendezvous pairs specific players.
- **The arena is game-agnostic and is being split into its own repo.** It
  speaks serialized states/actions as byte strings and never names a game.
  Do not add an `#include`, a bazel label or a hardcoded path under
  `game_mcts/tournament_server/` that points at `core/`, `games/` or
  `arena/` — that is the coupling the split exists to remove. Strings count:
  a bazel label in a generated BUILD or a game name in the coordinator is the
  same mistake as a dep.
- Rules reach the arena through `tournament_broker::GameRegistry()`, which
  `referee/game_registry.h` **declares and never defines**. A referee, broker
  or client binary is assembled as "a registry + an entry-point library"
  (`referee:referee_main`, `referee:broker_server_main`,
  `client:random_client_main`) — see `//game_mcts/arena:match_referee` and
  `//game_mcts/tournament_server/testgame:match_referee`. Registry libraries
  need `alwayslink = 1`; nothing depends on them by label.
- Anything problem-specific belongs in the problem's `.textproto`, not in the
  coordinator: `submission.harness` carries the labels a generated candidate
  BUILD is written against, `submission.allowed_dep_prefixes` the deps a
  solution may name, `match.referee_target` the referee to build.
- `server:no_problem_code_test` nm-scans `problem_server` for
  `GameRegistry`/`GameSession`; it must keep passing.
- `//game_mcts/tournament_server/testgame` (Nim) is the arena's own game, so
  the broker can be tested with no game framework in the build. Use it when
  adding arena-side coverage; use `game_mcts/arena` for coverage that needs a
  real game.
- `PlayRemoteGames` owns the `Play`-stream protocol (including
  drain-before-`Finish`); hand-rolled clients must replicate that.
- Prefer the MCP tools (`arena_rules()` first, then submit by **paths**,
  never pasted code) and the `dev_bot` local loop over hand-rolling gRPC.
- `random_client.cc` is the reference client; `risk_mcts_client.cc` the
  reference C++ bot.

## Do not

- Do not add virtuals to the game/search hot path; do not put policy
  inside the game class; do not approximate inside
  `sample_chance_action` or the tree (shortcuts are rollout-only).
- Do not use `#pragma once`, do not hand-write a guard without the
  `GAME_MCTS_` prefix, do not commit unformatted code or notebook outputs
  (pre-commit handles both — let it run).
- Do not commit `bazel-*` symlinks/outputs, `compile_commands.json`,
  `.cache`, venvs, or generated proto artifacts (all git-ignored).
- Do not invent Bazel target names or include paths — `glob`/`grep` the
  nearest `BUILD` file and existing `#include`s first.
