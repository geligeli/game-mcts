# game-mcts

A header-only, concept-based (C++20) framework for turn-based games and
Monte Carlo Tree Search (MCTS), plus example games and tooling built on it.
Everything is C++23, built with Bazel (`cc_library` / `cc_binary` /
`cc_test`).

The framework strictly separates **rules** (the game class: state + action
-> successor), **policy** (an external `ActionProposer` choosing moves) and
**search** (`MctsRunner`, the tree + selection/rollout/backpropagation). All
contracts are C++20 concepts — no abstract base classes, no virtual dispatch
on the hot path.

## Layout

- `game_mcts/core/mcts/` — the framework: game concepts, game runner, MCTS,
  rollouts, tournaments, tree export. See
  [game_mcts/core/mcts/README.md](game_mcts/core/mcts/README.md) for the design
  overview, the contracts, and step-by-step guides for implementing a game
  and running MCTS on it. Helpers live in `game_mcts/core/util/`, the Python
  driving interfaces in `game_mcts/core/python/`.
- `game_mcts/games/tictactoe/` — minimal deterministic example game; the
  smallest complete example to copy from.
- `game_mcts/games/pig/` — minimal game with chance nodes (dice).
- `game_mcts/games/risk/` — a full-size stochastic game (Risk) with a custom
  action proposer, rollout shortcuts, self-play and tournament binaries.
- `game_mcts/common/fitters/` — simple curve fitters.
- `game_mcts/tools/viz/` — HTML/HTTP plot serving.
- `game_mcts/tools/bench/` — benchmark binaries.
- `game_mcts/tools/ascii_rendering/` — Python pipeline producing the ASCII
  board templates compiled into `games/risk/ascii`.
- `game_mcts/common/numpy/` — header-only `.npy` reader.

See [game_mcts/README.md](game_mcts/README.md).

The repo is also a problem repo for
[game-arena](https://github.com/geligeli/game-arena), which hosts **risk2**:
participants submit a Risk strategy that is built in a sandbox and rated
against the field. `problem.textproto` and `arena_problem` in `//:BUILD`
define it; `problem/` holds the game registry, `bots/` the submission harness
and the reference bot. Nothing under `game_mcts/` depends on the arena.

The board-vision, pose and training pipeline that feeds a physical Risk board
lives in the [risk-game-ai](https://github.com/geligeli/risk-game-ai) repo,
which consumes this one as a Bazel dependency.

## Build and test

```sh
bazel build //...
bazel test //...
```

Machine-specific settings go in a git-ignored `.bazelrc.local` (never in
`.bazelrc`, which ships into the arena's sandbox image and kits), e.g. the
shared caches:

```
common --disk_cache=/large_nfs/bazel-cache/disk
common --repository_cache=/large_nfs/bazel-cache/repo
common --experimental_disk_cache_gc_max_size=50G
```

## Quickstart: a risk2 tournament with coding agents

```sh
export CLAUDE_CODE_OAUTH_TOKEN=...   # `claude setup-token`
./quickstart.sh --local              # or without --local: push to registry.takumi.city
```

One tmux session: the coordinator (dashboard on http://localhost:8090, with
coloured replays; `GRPC_PORT` / `HTTP_PORT` move it beside another
tournament), a sandbox worker, and a kit container per player
(`PLAYERS="alice bob"` by default), each running Claude Code on the standing
mission in `problem/mission.md`: improve `bots/<name>/`, spar briefly, submit.
Only submissions are rated. The agents' base image is game-arena's; its
`quickstart.sh` builds it and `MODULE.bazel` pins it by digest.

The dashboard's replays (`/games/<id>`, `#<move>` for one move) draw each step
on the Risk map in the browser: owners, armies, an arrow for an attack or a
fortify, the dice and who they cost. That is `problem/risk2_replay.js`, served
by the coordinator with the map (`problem/risk_map.svg`), from a ~200-byte JSON
view the session records per step. Replay a recorded game in the terminal
instead, with the same captions and ANSI boards (games land in
`~/.arena/risk2/games/`, or a referee's `--scratch_dir`):

```sh
bazel run //problem:risk_replay -- --play --delay_ms=300 GAME.pb
```

Smaller loops: one local match, or a tournament with a kit shell for you:

```sh
bazel run //:match_referee -- --game=risk2 --player_a=reference --games=2 &
bazel run //bots/reference:bot -- --name=reference --opponent=builtin:mcts --games=2
bazel run //:play
```

Run the MCTS benchmark or the convergence-speed plot:

```sh
bazel run //game_mcts/tools/bench:mcts_bench
bazel run //game_mcts/tools/bench:mcts_convergence_speed
```

## Consuming this repo

Bazel labels look like `//game_mcts/core/mcts:mcts`; headers are
included with the full repo-relative path, e.g.
`#include "game_mcts/core/mcts/mcts.h"`.

New here? Read [game_mcts/core/mcts/README.md](game_mcts/core/mcts/README.md) —
it explains the rules/policy/search separation, and
`game_mcts/games/tictactoe/` is the smallest complete example to copy from.
