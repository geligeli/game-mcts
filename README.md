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

- `game_mcts/cpp/mcts/` — the framework: game concepts, game runner, MCTS,
  rollouts, tournaments, tree export, Python bindings. See
  [game_mcts/cpp/mcts/README.md](game_mcts/cpp/mcts/README.md) for the design
  overview, the contracts, and step-by-step guides for implementing a game
  and running MCTS on it.
- `game_mcts/cpp/tictactoe/` — minimal deterministic example game; the
  smallest complete example to copy from.
- `game_mcts/cpp/pig_game/` — minimal game with chance nodes (dice).
- `game_mcts/cpp/process/` — small subprocess wrapper.
- `game_mcts/cpp/fitters/` — simple curve fitters.
- `game_mcts/cpp/visualisations/` — HTML/HTTP plot serving.
- `game_mcts/cpp/numpy/` — header-only `.npy` reader.

See [game_mcts/cpp/README.md](game_mcts/cpp/README.md).

A full-size stochastic game (Risk) with a custom action proposer, rollout
shortcuts, self-play and a gRPC tournament server lives in the
[risk-game-ai](https://github.com/geligeli/risk-game-ai) repo, which consumes
this one as a Bazel dependency.

## Build and test

```sh
bazel build //...
bazel test //...
```

Run the MCTS benchmark or the convergence-speed plot:

```sh
bazel run //game_mcts/cpp/mcts:mcts_bench
bazel run //game_mcts/cpp/mcts:mcts_convergence_speed
```

## Consuming this repo

Bazel labels look like `@game_mcts//game_mcts/cpp/mcts:mcts`; headers are
included with the `game_mcts/cpp/...` prefix, e.g.
`#include "game_mcts/cpp/mcts/mcts.h"`.

New here? Read [game_mcts/cpp/mcts/README.md](game_mcts/cpp/mcts/README.md) —
it explains the rules/policy/search separation, and
`game_mcts/cpp/tictactoe/` is the smallest complete example to copy from.
