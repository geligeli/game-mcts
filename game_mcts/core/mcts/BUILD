load("@protobuf//bazel:cc_proto_library.bzl", "cc_proto_library")
load("@protobuf//bazel:proto_library.bzl", "proto_library")
load("@protobuf//bazel:py_proto_library.bzl", "py_proto_library")
load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@rules_cc//cc:cc_test.bzl", "cc_test")
load("@rules_python//python:defs.bzl", "py_library")

package(
    default_visibility = ["//visibility:public"],
)

cc_library(
    name = "game_traits",
    srcs = ["game_traits.cpp"],
    hdrs = ["game_traits.h"],
    deps = ["@abseil-cpp//absl/log"],
)

cc_library(
    name = "game_runner",
    hdrs = ["game_runner.h"],
    deps = [":game_traits"],
)

cc_library(
    name = "mcts",
    srcs = ["mcts.cpp"],
    hdrs = [
        "mcts.h",
        "mcts.inl",
    ],
    deps = [
        ":game_traits",
        "//game_mcts/core/util:overloaded",
        "@abseil-cpp//absl/log",
    ],
)

cc_library(
    name = "minimax",
    hdrs = [
        "minimax.h",
    ],
    deps = [
        ":game_traits",
        "//game_mcts/core/util:overloaded",
        "@abseil-cpp//absl/log",
    ],
)

cc_library(
    name = "tournament",
    hdrs = ["tournament.h"],
    deps = [":game_traits"],
)

proto_library(
    name = "mcts_tree_proto",
    srcs = ["mcts_tree.proto"],
)

cc_proto_library(
    name = "mcts_tree_cc_proto",
    deps = [":mcts_tree_proto"],
)

# Python bindings for mcts_tree.proto (PyMcts.export_tree());
# lands at bazel-bin/game_mcts/core/mcts/mcts_tree_pb2.py (or under
# external/game_mcts+/... when consumed from another repo).
py_proto_library(
    name = "mcts_tree_py_proto",
    visibility = ["//visibility:public"],
    deps = [":mcts_tree_proto"],
)

# py_proto_library doesn't add its package dir to the Python import path, so
# wrap it to make "import mcts_tree_pb2" work for consumers.
py_library(
    name = "mcts_tree_py",
    visibility = ["//visibility:public"],
    deps = [":mcts_tree_py_proto"],
    imports = ["."],
)

cc_library(
    name = "serialization",
    hdrs = ["serialization.h"],
    deps = [":game_traits"],
)

cc_library(
    name = "mcts_export",
    hdrs = ["mcts_export.h"],
    deps = [
        ":mcts",
        ":mcts_tree_cc_proto",
        ":serialization",
    ],
)

cc_test(
    name = "mcts_export_test",
    srcs = ["mcts_export_test.cpp"],
    deps = [
        ":mcts",
        ":mcts_export",
        ":mcts_tree_cc_proto",
        ":serialization",
        "//game_mcts/games/tictactoe",
        "//game_mcts/games/tictactoe:tictactoe_serialization",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "tournament_test",
    srcs = ["tournament_test.cpp"],
    deps = [
        ":mcts",
        ":tournament",
        "//game_mcts/games/tictactoe",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "mcts_test",
    srcs = ["mcts_test.cpp"],
    deps = [
        ":mcts",
        "//game_mcts/common/process",
        "//game_mcts/games/tictactoe",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "minimax_test",
    srcs = [
        "minimax_test.cpp",
    ],
    linkstatic = 1,
    deps = [
        ":game_traits",
        ":minimax",
        "//game_mcts/core/util:overloaded",
        "//game_mcts/games/tictactoe",
        "@abseil-cpp//absl/log",
        "@googletest//:gtest_main",
    ],
)
