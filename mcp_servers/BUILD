load("@rules_python//python:defs.bzl", "py_binary")

exports_files(["requirements.txt"])

# Connects to both servers over stdio using the commands from
# .kimi-code/mcp.json, lists tools, and calls one tool per server.
# Run: bazel run //mcp_servers:smoke_test
py_binary(
    name = "smoke_test",
    srcs = ["smoke_test.py"],
    deps = ["@mcp_pip_deps//mcp"],
)
