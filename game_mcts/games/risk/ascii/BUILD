load("@rules_cc//cc:cc_binary.bzl", "cc_binary")
load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@rules_cc//cc:cc_test.bzl", "cc_test")

package(default_visibility = ["//visibility:public"])

# ASCII rendering of the Risk board. The pre-generated templates are compiled
# in from //game_mcts/tools/ascii_rendering (the Python pipeline that produces
# them lives there too).

cc_library(
    name = "ascii_board",
    srcs = ["ascii_board.cpp"],
    hdrs = ["ascii_board.h"],
)

cc_test(
    name = "ascii_board_test",
    srcs = ["ascii_board_test.cpp"],
    deps = [
        ":ascii_board",
        ":ascii_board_templates",
        "@abseil-cpp//absl/log",
        "@googletest//:gtest_main",
    ],
)

genrule(
    name = "gen_ascii_board_templates",
    srcs = ["//game_mcts/tools/ascii_rendering:template_files"],
    outs = ["ascii_board_templates.cpp"],
    cmd = "python3 $(location //game_mcts/tools/ascii_rendering:template_to_cpp.py) -o $@ $(locations //game_mcts/tools/ascii_rendering:template_files)",
    tools = ["//game_mcts/tools/ascii_rendering:template_to_cpp.py"],
)

cc_library(
    name = "ascii_board_templates",
    srcs = [":gen_ascii_board_templates"],
    deps = [":ascii_board"],
)

cc_binary(
    name = "ascii_board_demo",
    srcs = ["ascii_board_demo.cpp"],
    deps = [
        ":ascii_board",
        ":ascii_board_templates",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/flags:parse",
        "@abseil-cpp//absl/log:initialize",
    ],
)
