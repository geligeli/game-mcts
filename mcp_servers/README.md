# MCP servers

Two MCP servers for this repo, registered in `.kimi-code/mcp.json` and
`.claude/mcp.json` (project-level; requires workspace trust, joins new sessions
only):

- **risk-engine** (`risk_mcp/server.py`) — drives Risk games and MCTS
  through the `risk_engine` pybind module
  (`//game_mcts/games/risk:risk_engine`): apply/check actions (proto-text
  `RiskAction`), roll chance nodes, describe the board with territory names,
  query the MCTS visit-count policy, or let MCTS play a move. Positions can be
  loaded from proto-text `RiskState` dumps (the self-play binary's `s` key).
- **arena** (`arena_mcp/server.py`) — the strategy tournament: submit a
  candidate, poll its build, read any rival's source, queue matches, read the
  standings. A thin shim over the tournament server's Arena gRPC service (see
  `game_mcts/tournament_server/ARENA.md`), so it needs that server running —
  `ARENA_MCP_TARGET` points at it. `arena_submit` takes **paths, not file
  contents**, so an agent never pastes back code it just wrote, and build
  failures return extracted compiler errors rather than the bazel log. Start
  with `arena_rules()`.

## Setup / reinstall

The venv is git-ignored. If this system lacks `ensurepip`, use `virtualenv`
(not `python -m venv`). `mcp` must stay on 1.x — the servers import
`mcp.server.fastmcp`, removed in mcp 2.0.

```sh
python3 -m virtualenv mcp_servers/.venv
mcp_servers/.venv/bin/pip install "mcp>=1.0,<2" "protobuf==6.32.1" \
    "grpcio>=1.60" "grpcio-tools>=1.60"
```

The arena server needs generated gRPC stubs (git-ignored; regenerate after
changing `game_mcts/tournament_server/proto/arena.proto`). This workspace has
`py_proto_library` but no Python gRPC rules, so codegen is a script rather than
a bazel target:

```sh
mcp_servers/arena_mcp/make_stubs.sh
```

The risk-engine server needs the bazel-built artifacts (rebuild after C++
changes; the .so is loaded from `bazel-bin`):

```sh
bazel build //game_mcts/games/risk:risk_engine //game_mcts/games/risk:risk_py_proto
```

## Smoke test

Connects to each server over stdio, lists tools, and calls one:

```sh
mcp_servers/.venv/bin/python mcp_servers/smoke_test.py
```
