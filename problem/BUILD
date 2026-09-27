load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@rules_cc//cc:cc_test.bzl", "cc_test")

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

# alwayslink: nothing depends on it by label; it defines a symbol the arena declares.
cc_library(
    name = "registry",
    srcs = ["registry.cc"],
    alwayslink = 1,
    deps = [
        ":session",
        "//game_mcts/core/mcts:policies",
        "//game_mcts/games/risk:risk_game",
        "//game_mcts/games/risk:risk_serialization",
        "//game_mcts/games/risk/strategies:risk_proposer",
        "//game_mcts/games/risk/strategies:risk_rollout_shortcuts",
        "@abseil-cpp//absl/log",
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
    name = "registry_test",
    srcs = ["registry_test.cc"],
    deps = [
        ":registry",
        "@game_arena//game_arena/referee:game_registry",
        "@googletest//:gtest_main",
    ],
)

# What a participant gets of this package (arena_problem's kit_files).
filegroup(
    name = "kit",
    srcs = glob([
        "BUILD",
        "*.cc",
        "*.h",
    ]),
)

# A glob does not cross packages, so each exports its own (arena_problem's `tree`).
filegroup(
    name = "tree",
    srcs = glob(["**"]),
)
