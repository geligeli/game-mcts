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
- `game_mcts/arena/` — this repo's side of the **arena**, the problem-running
  framework that now lives in
  [game-arena](https://github.com/geligeli/game-arena): the
  `mcts::SerializableGame` adapter, the builtins, the registry naming risk2 /
  tictactoe / bench and the referee binaries built from it, plus `client/`,
  `candidate_api/`, `candidates/`, `benchgame/` and the problem configs.
  Agents submit a strategy that is built in a sandbox and rated against the
  field; see [game_mcts/arena/README.md](game_mcts/arena/README.md).
- `game_mcts/common/fitters/` — simple curve fitters.
- `game_mcts/tools/viz/` — HTML/HTTP plot serving.
- `game_mcts/tools/bench/` — benchmark binaries.
- `game_mcts/tools/ascii_rendering/` — Python pipeline producing the ASCII
  board templates compiled into `games/risk/ascii`.
- `game_mcts/common/numpy/` — header-only `.npy` reader.

See [game_mcts/README.md](game_mcts/README.md).

The board-vision, pose and training pipeline that feeds a physical Risk board
lives in the [risk-game-ai](https://github.com/geligeli/risk-game-ai) repo,
which consumes this one as a Bazel dependency.

## Build and test

```sh
bazel build //...
bazel test //...
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
