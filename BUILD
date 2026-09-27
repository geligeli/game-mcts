load("@game_arena//game_arena/rules:problem.bzl", "arena_problem")

exports_files([
    "cpp_format.yaml",  # read by the cpp_format aspect (tools/cpp_format.sh)
    "protobuf_bzlmod_fixes.patch",
])

# risk2 as an arena problem: the macro defines :match_referee, :config_test,
# :tournament, :kit, :play, and the sandbox and kit images (see
# @game_arena//game_arena/rules:problem.bzl for the full list).
arena_problem(
    name = "risk2",
    config = "problem.textproto",
    registry = "//problem:registry",
    # A participant's workspace: the harness, the reference bot, the registry
    # (so `arena_cli spar` can referee locally) and what they link against. A
    # kit builds //..., so this is closed under every shipped target's deps,
    # tests included; the protobuf patch is read by MODULE.bazel's override.
    kit_files = [
        "protobuf_bzlmod_fixes.patch",
        "//bots:kit",
        "//bots/reference:kit",
        "//problem:kit",
        "//game_mcts/core/mcts:tree",
        "//game_mcts/core/python:tree",
        "//game_mcts/core/util:tree",
        "//game_mcts/games/tictactoe:tree",
        "//game_mcts/games/risk:tree",
        "//game_mcts/games/risk/ascii:tree",
        "//game_mcts/games/risk/strategies:tree",
        "//game_mcts/tools/ascii_rendering:tree",
    ],
    # Every package the bots and the referee build from, beyond this one.
    # bots/reference is left out: it is a participant, not the rules.
    tree = [
        "//bots:tree",
        "//problem:tree",
        "//game_mcts/core/mcts:tree",
        "//game_mcts/core/util:tree",
        "//game_mcts/games/risk:tree",
        "//game_mcts/games/risk/ascii:tree",
        "//game_mcts/games/risk/strategies:tree",
        "//game_mcts/tools/ascii_rendering:tree",
    ],
)
