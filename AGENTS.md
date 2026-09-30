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

Dependency direction: `common` <- `core` <- `games`, `tools` at the top
(nothing may depend on `tools`). The repo-root `problem/` and `bots/` packages
sit above `game_mcts/` and host risk2 in `@game_arena` (the problem-running
framework, its own repo); nothing under `game_mcts/` depends on them or on the
arena.

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
- Sanitizer configs in `.bazelrc` (each gets its own output dir):
  `--config=asan`, `--config=tsan`, `--config=ubsan`. No msan: the toolchain
  has no msan-instrumented libc++. No `-march=native` config: its objects
  would poison a cache shared across machines (both explained in `.bazelrc`).
  Example: `bazel test --config=asan //game_mcts/games/risk/...`.
- Python: games expose a `risk_engine`-style pybind module plus
  `py_proto_library` targets. After C++ changes rebuild, e.g.
  `bazel build //game_mcts/games/risk:risk_engine //game_mcts/games/risk:risk_py_proto`.
  Canonical Python test example: `game_mcts/games/risk/risk_engine_test.py`.

## C++ conventions

- C++23 (`-std=c++23` in `.bazelrc`), `-Wall -Wextra -Werror` for project
  files; third-party under `external/` is silenced with `-w`, never "fix"
  warnings there. A warning clang raises *inside* an external header included
  from project code is disabled by category in `.bazelrc`, not by editing the
  dependency.
- The compiler is hermetic-llvm (clang, libc++, compiler-rt; no sysroot,
  nothing from the host), registered by `@game_arena` so this repo, a kit and
  the sandbox image all build with the same one. Machine-specific flags
  (the `/large_nfs` caches) go in the git-ignored `.bazelrc.local`, never in
  `.bazelrc`: that file ships into the sandbox image and every kit. `.bazelrc` turns off bazel's
  host C++ autodetection, which is what lets the build run on RBE workers with
  no compiler installed. Sanitizer configs use its
  `--@llvm//config:<san>=true` settings rather than raw `-fsanitize` flags.
  libc++ is stricter about transitive includes than libstdc++: include what
  you use (`<array>`, `<cstdint>`, ...).
- Format: `clang-format -i` (pre-commit hook), with no `-style` flag so the
  repo's `.clang-format` is what applies: Google style plus
  `AlwaysBreakTemplateDeclarations: Yes`, `IncludeBlocks: Preserve`,
  `DerivePointerAlignment: true` — keep them.
- Naming and return types follow `cpp_format.yaml` (the arena's: leading
  return types, snake_case locals, `trailing_` members, UpperCamelCase
  methods, `kConstants`). It is a Bazel aspect, too slow for a hook: run
  `tools/cpp_format.sh check` (or `diff` / `fix`) before sending a change
  up. A rename it cannot see through a template's `.inl` breaks the build;
  fix those references by hand. `tools/cpp_format.sh compile_commands`
  refreshes `compile_commands.json`.
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
- Stock policies (`ProposerPolicy`, `MctsPolicy`) and `SerializedPolicy`
  (any `TournamentPolicy` as serialized state bytes -> action bytes, the
  shape of an arena builtin and of a bot's move) live in
  `game_mcts/core/mcts/policies.h`.

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

## The arena (agents)

The arena is `@game_arena`, a separate repo
(https://github.com/geligeli/game-arena) consumed as a bazel module
(`dev_dependency`, so repos consuming game_mcts never resolve it), pinned to a
commit with `git_override` in `MODULE.bazel`. It is a general problem-running
framework and knows nothing about this repo. Changing it means landing it there
and bumping the pin here; to iterate on both at once, swap in the commented
`local_path_override`.

This repo is a problem repo in the arena's `examples/connect4` layout:

- `//:BUILD` calls `arena_problem(name = "risk2", ...)` with
  `problem.textproto`. It defines `:match_referee`, `:config_test`,
  `:tournament`, `:kit`, `:play` and the `sandbox_image*` / `kit_image*`
  targets (see `@game_arena//game_arena/rules:problem.bzl`).
- `problem/`: `session.h` (the `mcts::SerializableGame` -> `GameSession`
  adapter) and `registry.cc`, which **defines** the `GameRegistry()` that
  `@game_arena//game_arena/referee:game_registry` only declares
  (`alwayslink = 1`). Builtins (`random`, `mcts[:iterations=N]`,
  `mcts_smart[:iterations=N]`, tuning_result.md's strongest) are stock
  policies through `SerializedPolicy`. Adding a game or builtin is an entry
  there; if you think game-arena has to change for it, the seam is wrong.
- `problem/risk_session.h`: risk2 on top of the rules. After `max_rounds`
  (a registry option) the game is won on territories, then armies; an exact
  tie is left to the referee's time tiebreak. The replay: `RenderLastStep()`
  is every step's caption, `RenderState()` a JSON snapshot of the board it
  left, with what it touched and its dice (`problem/risk_view.h`, ~200 B).
  `problem/risk_replay.js` draws it on `problem/risk_map.svg` in the
  dashboard's browser (`arena_problem`'s `replay_assets`/`replay_module`);
  it is plain JavaScript with no build step, and the host has no JS runtime,
  so check it in a browser (headless Chrome can screenshot `/games/<id>#<n>`).
  The map is generated by `game_mcts/tools/ascii_rendering/export_board_svg.py`
  from risk-game-ai's board annotation; `//problem:risk_map_test` guards it.
  A whole `GameRecord` reaches the coordinator as one gRPC message (up to
  64 MiB), and a game's views are ~100-300 KB. The captions and the terminal's
  ANSI boards come from `game_mcts/games/risk/risk_render.h`, shared with
  `//problem:risk_replay` (`--mode=full`, `--play`, `--stats`).
- `bots/`: the submission contract. `bot_api.h` (`MakePolicy(const
  candidate::Params&) -> candidate::policy_t`), `bot.cc` (the harness, on
  `@game_arena//game_arena/client:play_loop`), `bot_deps`. Each participant
  is `bots/<name>/` with the BUILD the arena generates; `bots/reference/` is
  the starter everyone is copied from.
- `kit_files` / `tree` in `//:BUILD` list the packages shipped to kits and to
  the sandbox image; every such package has a `tree` filegroup. A kit builds
  `//...`, so `kit_files` must stay closed under the deps of every shipped
  target, tests included: check with `bazel run //:kit -- --out=/tmp/kit`,
  which builds it. `REPO.bazel` keeps `.git` & co. out of the sandbox tree.
- Problem settings (harness labels, `allowed_dep_prefixes`, sandbox image
  tag, match timing) live in `problem.textproto`. Bump `sandbox.image`'s tag
  whenever the tree or toolchain changes; `.bazelversion` must stay the bazel
  the arena's base image installs, and `MODULE.bazel.lock` committed and
  current (vendoring runs with `--lockfile_mode=error`).
- Local match: `bazel run //:match_referee -- --game=risk2
  --player_a=reference --games=2` and, against its port, `bazel run
  //bots/reference:bot -- --name=reference --opponent=builtin:mcts`. Opponents
  are `builtin:<spec>` or `player:<name>` (both sides name each other).
  Whole tournament with a kit shell: `bazel run //:play`; with coding agents
  as the players: `./quickstart.sh [--local]`, which starts each on
  `problem/mission.md` (claude, agy and opencode; workers on geli-3950's
  docker). Protocol, referee
  flags and the submission loop: `game_arena/README.md` and
  `game_arena/ARENA.md` in the arena repo.

## Do not

- Do not push game_mcts code into game-arena, do not make anything under
  `game_mcts/` depend on `@game_arena`, `problem/` or `bots/`, and do not
  reach into `@game_arena` beyond its kit surface (`proto`, `referee`,
  `client`, `common/kv_options`, `rules`): a kit and the sandbox get nothing
  else.
- Do not add virtuals to the game/search hot path; do not put policy
  inside the game class; do not approximate inside
  `sample_chance_action` or the tree (shortcuts are rollout-only).
- Do not use `#pragma once`, do not hand-write a guard without the
  `GAME_MCTS_` prefix, do not commit unformatted code (pre-commit handles
  guards and formatting — let it run). Nothing clears notebook outputs for
  you: clear them by hand before committing an `.ipynb`.
- Do not commit `bazel-*` symlinks/outputs, `compile_commands.json`,
  `.cache`, venvs, or generated proto artifacts (all git-ignored).
- Do not invent Bazel target names or include paths — `glob`/`grep` the
  nearest `BUILD` file and existing `#include`s first.
