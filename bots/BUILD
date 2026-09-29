load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@rules_cc//cc:cc_test.bzl", "cc_test")

package(default_visibility = ["//visibility:public"])

# The risk2 submission contract (submission.harness in problem.textproto).
# Each participant is a directory here, with the BUILD the arena generates;
# reference/ is the one everybody starts from.

cc_library(
    name = "bot_api",
    hdrs = ["bot_api.h"],
    deps = [
        "//game_mcts/core/mcts:policies",
        "//game_mcts/core/mcts:tournament",
        "//game_mcts/games/risk:risk_game",
        "//game_mcts/games/risk/strategies:risk_proposer",
        "//game_mcts/games/risk/strategies:risk_rollout_shortcuts",
        "@game_arena//game_arena/common/kv_options",
    ],
)

# A source, not a cc_library: the generated BUILD compiles it with the entry
# header as a local_define, which does not propagate from a dependency.
exports_files(["bot.cc"])

# What the generated per-submission binary links (submission.harness.bot_deps).
cc_library(
    name = "bot_deps",
    deps = [
        ":bot_api",
        "//game_mcts/core/mcts:policies",
        "//game_mcts/games/risk:risk_serialization",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/flags:parse",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:globals",
        "@abseil-cpp//absl/log:initialize",
        "@game_arena//game_arena/client:play_loop",
        "@game_arena//game_arena/proto:tournament_broker_cc_grpc",
        "@grpc//:grpc++",
    ],
)

cc_test(
    name = "bot_api_test",
    srcs = ["bot_api_test.cc"],
    deps = [
        ":bot_api",
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
