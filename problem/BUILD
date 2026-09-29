load("@rules_cc//cc:cc_binary.bzl", "cc_binary")
load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@rules_cc//cc:cc_test.bzl", "cc_test")
load("@rules_python//python:defs.bzl", "py_test")

package(default_visibility = ["//visibility:public"])

# This repo's side of the arena: any mcts::SerializableGame as a GameSession,
# and the registry naming the games the referee plays. Nothing under game_mcts/
# may depend on this package or on @game_arena.

cc_library(
    name = "session",
    hdrs = ["session.h"],
    deps = [
        "//game_mcts/core/mcts:game_traits",
        "//game_mcts/core/mcts:serialization",
        "@abseil-cpp//absl/log:check",
        "@game_arena//game_arena/referee:game_session",
    ],
)

# risk2's session: the round cap and the replay view on top of the adapter.
cc_library(
    name = "risk_session",
    hdrs = ["risk_session.h"],
    deps = [
        ":risk_view",
        ":session",
        "//game_mcts/core/mcts:serialization",
        "//game_mcts/games/risk:risk_game",
        "//game_mcts/games/risk:risk_serialization",
        "@game_arena//game_arena/referee:game_session",
    ],
)

# The round cap, captions and views without protobuf or the arena: shared by
# the session above and web/'s in-browser engine.
cc_library(
    name = "risk_view",
    hdrs = ["risk_view.h"],
    deps = [
        "//game_mcts/core/mcts:game_traits",
        "//game_mcts/games/risk:risk_game",
        "//game_mcts/games/risk:risk_render",
    ],
)

# alwayslink: nothing depends on it by label; it defines a symbol the arena declares.
cc_library(
    name = "registry",
    srcs = ["registry.cc"],
    alwayslink = 1,
    deps = [
        ":risk_session",
        "//game_mcts/core/mcts:policies",
        "//game_mcts/games/risk:risk_game",
        "//game_mcts/games/risk:risk_serialization",
        "//game_mcts/games/risk/strategies:risk_proposer",
        "//game_mcts/games/risk/strategies:risk_rollout_shortcuts",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/strings",
        "@game_arena//game_arena/referee:game_registry",
    ],
)

cc_test(
    name = "session_test",
    srcs = ["session_test.cc"],
    deps = [
        ":session",
        "//game_mcts/core/mcts:minimax",
        "//game_mcts/games/risk:risk_game",
        "//game_mcts/games/risk:risk_serialization",
        "//game_mcts/games/risk/strategies:risk_proposer",
        "//game_mcts/games/tictactoe",
        "//game_mcts/games/tictactoe:tictactoe_cc_proto",
        "//game_mcts/games/tictactoe:tictactoe_serialization",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "risk_session_test",
    srcs = ["risk_session_test.cc"],
    deps = [
        ":risk_session",
        "//game_mcts/core/mcts:serialization",
        "//game_mcts/games/risk:risk_game",
        "//game_mcts/games/risk:risk_serialization",
        "//game_mcts/games/risk/strategies:risk_proposer",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "registry_test",
    srcs = ["registry_test.cc"],
    deps = [
        ":registry",
        "@game_arena//game_arena/referee:game_registry",
        "@googletest//:gtest_main",
    ],
)

# Replays recorded games in the terminal; --stats measures a game's views.
cc_binary(
    name = "risk_replay",
    srcs = ["risk_replay.cc"],
    deps = [
        ":risk_session",
        "//game_mcts/core/mcts:serialization",
        "//game_mcts/games/risk:risk_game",
        "//game_mcts/games/risk:risk_render",
        "//game_mcts/games/risk:risk_serialization",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/flags:parse",
        "@game_arena//game_arena/proto:tournament_broker_cc_proto",
    ],
)

# The browser replay (arena_problem's replay_assets): the module that draws a
# view, and the map it draws on (export_board_svg.py in
# game_mcts/tools/ascii_rendering made it).
filegroup(
    name = "replay_assets",
    srcs = [
        "risk2_replay.js",
        "risk_map.svg",
    ],
)

py_test(
    name = "risk_map_test",
    srcs = ["risk_map_test.py"],
    data = ["risk_map.svg"],
)

# What a participant gets of this package (arena_problem's kit_files).
filegroup(
    name = "kit",
    srcs = glob([
        "BUILD",
        "*.cc",
        "*.h",
        "*.js",
        "*.py",
        "*.svg",
    ]),
)

# A glob does not cross packages, so each exports its own (arena_problem's `tree`).
filegroup(
    name = "tree",
    srcs = glob(["**"]),
)
