# C++ Game Runner and MCTS

This directory contains a header-only, concept-based (C++20) framework for
turn-based games and Monte Carlo Tree Search (MCTS), plus two example games
and a few generic utilities built on it. Everything is built with Bazel
(`cc_library` / `cc_binary` / `cc_test`).

- `game_mcts/cpp/mcts/` — the framework: game concepts, game runner, MCTS,
  rollouts, tournaments, tree export, Python bindings. See
  [game_mcts/cpp/mcts/README.md](mcts/README.md) for the design overview, the
  contracts, and step-by-step guides for implementing a game and running
  MCTS on it.
- `game_mcts/cpp/tictactoe/` — minimal deterministic example game.
- `game_mcts/cpp/pig_game/` — minimal game with chance nodes (dice).
- `game_mcts/cpp/process/` — small subprocess wrapper (used by
  `mcts_test`).
- `game_mcts/cpp/fitters/` — simple curve fitters (used by
  `mcts_convergence_speed`).
- `game_mcts/cpp/visualisations/` — HTML/HTTP plot serving (used by
  `mcts_convergence_speed`).
- `game_mcts/cpp/numpy/` — header-only `.npy` reader.

A full-size stochastic game (Risk) with a custom action proposer, rollout
shortcuts, self-play and tournament binaries, plus a gRPC tournament broker
with an agent-submission **arena**, lives in the
[risk-game-ai](https://github.com/geligeli/risk-game-ai) repo.

New here? Read [game_mcts/cpp/mcts/README.md](mcts/README.md) — it explains
the rules/policy/search separation, and `game_mcts/cpp/tictactoe/` is the
smallest complete example to copy from.
