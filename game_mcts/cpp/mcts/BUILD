load("@protobuf//bazel:cc_proto_library.bzl", "cc_proto_library")
load("@protobuf//bazel:proto_library.bzl", "proto_library")
load("@protobuf//bazel:py_proto_library.bzl", "py_proto_library")
load("@pybind11_bazel//:build_defs.bzl", "pybind_library")
load("@rules_cc//cc:cc_binary.bzl", "cc_binary")
load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@rules_cc//cc:cc_test.bzl", "cc_test")

package(
    default_visibility = ["//visibility:public"],
)

cc_library(
    name = "default_dict",
    hdrs = [
        "default_dict.h",
    ],
    visibility = ["//visibility:public"],
)

cc_test(
    name = "default_dict_test",
    srcs = [
        "default_dict_test.cpp",
    ],
    deps = [
        ":default_dict",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "overloaded",
    hdrs = [
        "overloaded.h",
    ],
    visibility = ["//visibility:public"],
)

cc_library(
    name = "starts_and_bars",
    srcs = ["starts_and_bars.cpp"],
    hdrs = ["starts_and_bars.h"],
    visibility = ["//visibility:public"],
)

cc_test(
    name = "starts_and_bars_test",
    srcs = ["starts_and_bars_test.cpp"],
    deps = [
        ":starts_and_bars",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "state_space",
    srcs = [
        "state_space.cpp",
    ],
    hdrs = [
        "state_space.h",
    ],
    visibility = ["//visibility:public"],
    deps = [
        ":starts_and_bars",
        "@boost.iterator",
        "@boost.multiprecision",
    ],
)

cc_test(
    name = "state_space_test",
    srcs = [
        "state_space_test.cpp",
    ],
    deps = [
        ":state_space",
        "@googletest//:gtest_main",
    ],
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
        ":overloaded",
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
        ":overloaded",
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
# lands at bazel-bin/game_mcts/cpp/mcts/mcts_tree_pb2.py (or under
# external/game_mcts/... when consumed from another repo).
py_proto_library(
    name = "mcts_tree_py_proto",
    visibility = ["//visibility:public"],
    deps = [":mcts_tree_proto"],
)

cc_library(
    name = "serialization",
    hdrs = ["serialization.h"],
    deps = [":game_traits"],
)

# Type-erased driving interface (mcts::PyGame) over any ProtoSerializableGame:
# proto messages cross as serialized bytes, so a recorded game can be replayed
# (or driven live) through one game-agnostic API. Pybind-free; the pybind11
# glue is :py_game_binding.
cc_library(
    name = "py_game",
    hdrs = ["py_game.h"],
    deps = [":serialization"],
)

# pybind11 binding of the PyGame interface (mcts::BindPyGameClass). A game
# module calls it once and adds factory functions returning
# std::unique_ptr<mcts::PyGame>.
pybind_library(
    name = "py_game_binding",
    hdrs = ["py_game_binding.h"],
    deps = [":py_game"],
)

cc_test(
    name = "py_game_test",
    srcs = ["py_game_test.cpp"],
    deps = [
        ":py_game",
        "//game_mcts/cpp/tictactoe:tictactoe_cc_proto",
        "//game_mcts/cpp/tictactoe:tictactoe_serialization",
        "@googletest//:gtest_main",
    ],
)

# Type-erased MCTS search (mcts::PyMcts) over any ProtoSerializableGame, for
# the Python bindings: run/step, root visit-count policy, ExportTree bytes and
# a per-iteration observer callback. The observer is compile-time: production
# runners keep MctsRunner's default NullMctsObserver and pay nothing.
cc_library(
    name = "py_mcts",
    hdrs = ["py_mcts.h"],
    deps = [
        ":mcts",
        ":mcts_export",
        ":py_game",
        ":serialization",
    ],
)

# pybind11 binding of the PyMcts interface (mcts::BindPyMctsClass).
pybind_library(
    name = "py_mcts_binding",
    hdrs = ["py_mcts_binding.h"],
    deps = [":py_mcts"],
)

cc_test(
    name = "py_mcts_test",
    srcs = ["py_mcts_test.cpp"],
    deps = [
        ":mcts_tree_cc_proto",
        ":py_mcts",
        "//game_mcts/cpp/tictactoe:tictactoe_cc_proto",
        "//game_mcts/cpp/tictactoe:tictactoe_serialization",
        "@googletest//:gtest_main",
    ],
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
        "//game_mcts/cpp/tictactoe",
        "//game_mcts/cpp/tictactoe:tictactoe_serialization",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "tournament_test",
    srcs = ["tournament_test.cpp"],
    deps = [
        ":mcts",
        ":tournament",
        "//game_mcts/cpp/tictactoe",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "mcts_test",
    srcs = ["mcts_test.cpp"],
    deps = [
        ":mcts",
        "//game_mcts/cpp/process",
        "//game_mcts/cpp/tictactoe",
        "@googletest//:gtest_main",
    ],
)

cc_binary(
    name = "mcts_convergence_speed",
    srcs = ["mcts_convergence_speed.cpp"],
    deps = [
        ":mcts",
        "//game_mcts/cpp/fitters:simple_fitters",
        "//game_mcts/cpp/tictactoe",
        "//game_mcts/cpp/visualisations",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/flags:parse",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:initialize",
    ],
)

cc_binary(
    name = "mcts_bench",
    srcs = ["mcts_bench.cpp"],
    deps = [
        ":mcts",
        "//game_mcts/cpp/tictactoe",
        "@google_benchmark//:benchmark_main",
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
        ":overloaded",
        "//game_mcts/cpp/tictactoe",
        "@abseil-cpp//absl/log",
        "@googletest//:gtest_main",
    ],
)
