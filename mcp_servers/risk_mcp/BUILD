load("@rules_python//python:defs.bzl", "py_binary")

package(default_visibility = ["//visibility:public"])

# Risk engine MCP server. Run with: bazel run //mcp_servers/risk_mcp:server
# (reads risk_board.h via BUILD_WORKSPACE_DIRECTORY; the venv flow in
# mcp_servers/README.md still works unchanged).
py_binary(
    name = "server",
    srcs = ["server.py"],
    deps = [
        "//game_mcts/games/risk:risk_engine_lib",
        "//game_mcts/games/risk:risk_py",
        "@mcp_pip_deps//mcp",
    ],
)
