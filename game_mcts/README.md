# C++ Game Runner and MCTS

This directory contains a header-only, concept-based (C++20) framework for
turn-based games and Monte Carlo Tree Search (MCTS), the games built on it,
the gRPC tournament server, and supporting tools. Everything is built with
Bazel (`cc_library` / `cc_binary` / `cc_test`).

Dependency direction: `common` <- `core` <- `games` <- `arena`, with `tools` at
the top (nothing depends on `tools`). `arena` is this repo's side of
`@game_arena`, which depends on nothing here.

- `game_mcts/core/mcts/` — the framework: game concepts, game runner, MCTS,
  rollouts, tournaments, tree export. See
  [game_mcts/core/mcts/README.md](core/mcts/README.md) for the design
  overview, the contracts, and step-by-step guides for implementing a game
  and running MCTS on it.
- `game_mcts/core/util/` — generic helpers used by the framework
  (`overloaded`, `default_dict`, stars-and-bars combinatorics, state-space
  enumeration).
- `game_mcts/core/python/` — the type-erased `PyGame`/`PyMcts` driving
  interfaces and their pybind11 glue, used by games' Python bindings.
- `game_mcts/games/tictactoe/` — minimal deterministic example game.
- `game_mcts/games/pig/` — minimal game with chance nodes (dice).
- `game_mcts/games/risk/` — a full-size stochastic game (Risk) with a custom
  action proposer (`strategies/`), ASCII board rendering (`ascii/`), self-play
  and tournament binaries. See
  [game_mcts/games/risk/README.md](games/risk/README.md).
- `game_mcts/arena/` — the binding between this framework and
  [game-arena](https://github.com/geligeli/game-arena): the `GameSessionImpl`
  adapter, `builtins.h`, the game registry naming risk2 / tictactoe / bench,
  the referee and client binaries built from it, plus `client/`,
  `candidate_api/`, `candidates/`, `benchgame/` and `problems/`. See
  [README.md](arena/README.md) and [ARENA.md](arena/ARENA.md).
- `game_mcts/tools/bench/` — benchmark binaries (MCTS, Risk, broker
  throughput).
- `game_mcts/tools/viz/` — HTML/HTTP plot serving.
- `game_mcts/tools/ascii_rendering/` — the Python pipeline that turns board
  art into the ASCII templates compiled into `games/risk/ascii`.
- `game_mcts/common/fitters/` — simple curve fitters (used by
  `mcts_convergence_speed`).
- `game_mcts/common/numpy/` — header-only `.npy` reader.

New here? Read [game_mcts/core/mcts/README.md](core/mcts/README.md) — it
explains the rules/policy/search separation, and `game_mcts/games/tictactoe/`
is the smallest complete example to copy from.
