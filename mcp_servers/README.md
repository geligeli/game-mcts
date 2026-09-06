# MCP servers

One MCP server for this repo, registered in `.kimi-code/mcp.json` and
`.claude/mcp.json` (project-level; requires workspace trust, joins new sessions
only). The registration launches `bazel run //mcp_servers/...:server` — no venv
needed; the first launch of a cold bazel server may take a moment.

The arena MCP server moved to the
[game-arena](https://github.com/geligeli/game-arena) repo along with the arena
itself; run it from there.

- **risk-engine** (`risk_mcp/server.py`) — drives Risk games and MCTS
  through the `risk_engine` pybind module
  (`//game_mcts/games/risk:risk_engine`): apply/check actions (proto-text
  `RiskAction`), roll chance nodes, describe the board with territory names,
  query the MCTS visit-count policy, or let MCTS play a move. Positions can be
  loaded from proto-text `RiskState` dumps (the self-play binary's `s` key).

## Running under bazel (no venv needed)

The server is a `py_binary` using the hermetic 3.12 toolchain and pinned pip
deps from `mcp_servers/requirements.txt` (`pip.parse`, hub `@mcp_pip_deps`):

```sh
bazel run //mcp_servers/risk_mcp:server
```

Under `bazel run` it picks up `risk_engine`/`risk_pb2` from runfiles and uses
`BUILD_WORKSPACE_DIRECTORY` as the repo root (`RISK_MCP_REPO_ROOT` still wins
if set).

## Setup / reinstall (venv flow)

Fallback for machines without a working bazel, kept for reference.

The venv is git-ignored. If this system lacks `ensurepip`, use `virtualenv`
(not `python -m venv`). `mcp` must stay on 1.x — the servers import
`mcp.server.fastmcp`, removed in mcp 2.0.

```sh
python3 -m virtualenv mcp_servers/.venv
mcp_servers/.venv/bin/pip install "mcp>=1.0,<2" "protobuf==7.35.1" \
    "grpcio>=1.60" "grpcio-tools>=1.60"
```

(`mcp_servers/requirements.txt` has the same set, fully pinned, for bazel's
`pip.parse` — keep it in sync when bumping the venv install.)

The risk-engine server needs the bazel-built artifacts (rebuild after C++
changes; the .so is loaded from `bazel-bin`):

```sh
bazel build //game_mcts/games/risk:risk_engine //game_mcts/games/risk:risk_py_proto
```

## Smoke test

Connects to the server over stdio (using the command from
`.kimi-code/mcp.json`), lists its tools, and calls one:

```sh
bazel run //mcp_servers:smoke_test
```
