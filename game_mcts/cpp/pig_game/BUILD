load("@rules_cc//cc:cc_binary.bzl", "cc_binary")
load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@rules_cc//cc:cc_test.bzl", "cc_test")

package(
    default_visibility = ["//visibility:public"],
)

cc_library(
    name = "pig_game",
    srcs = [
        "pig_game.cpp",
    ],
    hdrs = [
        "pig_game.h",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "//game_mcts/cpp/mcts:game_traits",
    ],
)

cc_test(
    name = "pig_game_test",
    srcs = ["pig_game_test.cpp"],
    deps = [
        ":pig_game",
        "//game_mcts/cpp/mcts",
        "@boost.math",
        "@googletest//:gtest_main",
    ],
)
