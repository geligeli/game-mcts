load("@rules_cc//cc:cc_binary.bzl", "cc_binary")
load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@rules_cc//cc:cc_test.bzl", "cc_test")

package(default_visibility = ["//visibility:public"])

# Strategy policies for Risk: move proposers, rollout shortcuts, and the
# battle-outcome heuristics they are built on. All depend on //game_mcts/cpp/risk's game
# core, never the reverse.

# Action-proposal policy for RiskState (mcts::ActionProposer). Kept out of
# //game_mcts/cpp/risk:risk_game so the rules can be depended on without a policy.
cc_library(
    name = "risk_proposer",
    hdrs = ["risk_proposer.h"],
    deps = [
        "//game_mcts/cpp/mcts:game_traits",
        "//game_mcts/cpp/risk:risk_board",
        "//game_mcts/cpp/risk:risk_game",
    ],
)

# Move shortcuts for MCTS rollouts (approximations; the exact game logic stays
# in //game_mcts/cpp/risk:risk_game).
cc_library(
    name = "risk_rollout_shortcuts",
    hdrs = ["risk_rollout_shortcuts.h"],
    deps = [
        ":expected_battle_outcomes",
        "//game_mcts/cpp/risk:risk_game",
    ],
)

cc_library(
    name = "predict_battle_outcome",
    srcs = ["predict_battle_outcome.cpp"],
    hdrs = ["predict_battle_outcome.h"],
)

cc_test(
    name = "predict_battle_outcome_test",
    srcs = ["predict_battle_outcome_test.cpp"],
    deps = [
        ":predict_battle_outcome",
        "@googletest//:gtest_main",
    ],
)

cc_binary(
    name = "predict_battle_outcome_gen",
    srcs = ["predict_battle_outcome_gen.cpp"],
    deps = [
        ":predict_battle_outcome",
    ],
)

genrule(
    name = "generate_expected_battle_outcomes",
    srcs = [],
    outs = ["expected_battle_outcomes.cpp"],
    cmd = "$(location :predict_battle_outcome_gen) > $@",
    tools = [":predict_battle_outcome_gen"],
)

cc_library(
    name = "expected_battle_outcomes",
    srcs = ["expected_battle_outcomes.cpp"],
    hdrs = ["expected_battle_outcomes.h"],
    deps = [
        ":predict_battle_outcome",
    ],
)
