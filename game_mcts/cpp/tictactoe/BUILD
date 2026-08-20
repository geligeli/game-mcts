load("@protobuf//bazel:proto_library.bzl", "proto_library")
load("@protobuf//bazel:cc_proto_library.bzl", "cc_proto_library")
load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@rules_cc//cc:cc_test.bzl", "cc_test")

package(
    default_visibility = ["//visibility:public"],
)

cc_library(
    name = "tictactoe",
    srcs = ["tictactoe.cpp"],
    hdrs = ["tictactoe.h"],
    deps = [
        "//game_mcts/cpp/mcts:game_traits",
    ],
)

cc_test(
    name = "tictactoe_test",
    srcs = ["tictactoe_test.cpp"],
    deps = [
        ":tictactoe",
        "@googletest//:gtest_main",
    ],
)

proto_library(
    name = "tictactoe_proto",
    srcs = ["tictactoe.proto"],
)

cc_proto_library(
    name = "tictactoe_cc_proto",
    deps = [":tictactoe_proto"],
)

cc_library(
    name = "tictactoe_serialization",
    hdrs = ["tictactoe_serialization.h"],
    deps = [
        ":tictactoe",
        ":tictactoe_cc_proto",
        "//game_mcts/cpp/mcts:serialization",
    ],
)
