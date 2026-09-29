load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@rules_cc//cc:cc_test.bzl", "cc_test")

package(default_visibility = ["//visibility:public"])

# risk2 in the browser: a 3D viewer for stored games and a page to play any
# candidate version, its policy compiled to WebAssembly (see README.md).
# Nothing in the problem, the kits or the sandbox depends on this package.

# One game between a person and a policy; native here, WebAssembly in web/bots.
cc_library(
    name = "risk_match",
    srcs = ["risk_match.cc"],
    hdrs = ["risk_match.h"],
    deps = [
        "//game_mcts/core/mcts:policies",
        "//game_mcts/core/mcts:tournament",
        "//game_mcts/core/util:overloaded",
        "//game_mcts/games/risk:risk_board",
        "//game_mcts/games/risk:risk_game",
        "//game_mcts/games/risk/strategies:risk_proposer",
        "//problem:risk_view",
    ],
)

cc_test(
    name = "risk_match_test",
    srcs = ["risk_match_test.cc"],
    deps = [
        ":risk_match",
        "//game_mcts/core/mcts:policies",
        "//game_mcts/games/risk:risk_board",
        "//game_mcts/games/risk/strategies:risk_proposer",
        "//game_mcts/games/risk/strategies:risk_rollout_shortcuts",
        "@googletest//:gtest_main",
    ],
)

# The wasm entry point: web/bots/BUILD (written by stage.py) compiles it once
# per candidate, with that candidate's header as CANDIDATE_ENTRY_HEADER.
exports_files(["bot_wasm.cc"])
