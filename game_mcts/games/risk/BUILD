load("@protobuf//bazel:cc_proto_library.bzl", "cc_proto_library")
load("@protobuf//bazel:proto_library.bzl", "proto_library")
load("@protobuf//bazel:py_proto_library.bzl", "py_proto_library")
load("@pybind11_bazel//:build_defs.bzl", "pybind_extension")
load("@rules_cc//cc:cc_binary.bzl", "cc_binary")
load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@rules_cc//cc:cc_test.bzl", "cc_test")
load("@rules_python//python:defs.bzl", "py_library", "py_test")

package(default_visibility = ["//visibility:public"])

# 1. The C++ Library (Logic only)
cc_library(
    name = "risk_board",
    hdrs = ["risk_board.h"],
)

# 1. The C++ Library (Logic only)
cc_library(
    name = "risk_game",
    srcs = ["risk_game.cpp"],
    hdrs = ["risk_game.h"],
    deps = [
        ":risk_board",
        "//game_mcts/core/mcts:game_traits",
        "//game_mcts/core/util:overloaded",
        "//game_mcts/games/risk/ascii:ascii_board",
        "//game_mcts/games/risk/ascii:ascii_board_templates",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
    ],
)

# Strategy policies (proposers, rollout shortcuts, battle-outcome heuristics)
# live in //game_mcts/games/risk/strategies.

# Python bindings: drives a Risk game from serialized state/action protos via
# the game-agnostic mcts::PyGame interface. Import name: risk_engine.
pybind_extension(
    name = "risk_engine",
    srcs = ["risk_pybind.cpp"],
    deps = [
        ":risk_cc_proto",
        ":risk_game",
        ":risk_serialization",
        "//game_mcts/core/python:py_game_binding",
        "//game_mcts/core/python:py_mcts_binding",
        "//game_mcts/games/risk/strategies:risk_proposer",
        "//game_mcts/games/risk/strategies:risk_rollout_shortcuts",
    ],
)

# pybind_extension produces a bare .so (no PyInfo); wrap it in a py_library
# so it can be imported by name, per the pybind11_bazel examples.
py_library(
    name = "risk_engine_lib",
    data = [":risk_engine"],
    imports = ["."],
)

# py_proto_library doesn't add its package dir to the Python import path, so
# wrap it to make "import risk_pb2" work for consumers.
py_library(
    name = "risk_py",
    deps = [":risk_py_proto"],
    imports = ["."],
)

py_test(
    name = "risk_engine_test",
    srcs = ["risk_engine_test.py"],
    deps = [
        ":risk_engine_lib",
        ":risk_py",
        "//game_mcts/core/mcts:mcts_tree_py",
    ],
    python_version = "PY3",
)

cc_test(
    name = "risk_game_test",
    srcs = ["risk_game_test.cpp"],
    deps = [
        ":risk_game",
        "//game_mcts/core/mcts:mcts",
        "//game_mcts/games/risk/strategies:expected_battle_outcomes",
        "//game_mcts/games/risk/strategies:risk_proposer",
        "//game_mcts/games/risk/strategies:risk_rollout_shortcuts",
        "@abseil-cpp//absl/log",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "risk_mcts_test",
    srcs = ["risk_mcts_test.cpp"],
    deps = [
        ":risk_game",
        "//game_mcts/core/mcts",
        "//game_mcts/games/risk/strategies:risk_proposer",
        "//game_mcts/games/risk/strategies:risk_rollout_shortcuts",
        "@googletest//:gtest_main",
    ],
)

cc_binary(
    name = "risk_mcts_selfplay",
    srcs = ["risk_mcts_selfplay.cpp"],
    deps = [
        ":risk_cc_proto",
        ":risk_game",
        ":risk_serialization",
        "//game_mcts/core/mcts:mcts",
        "//game_mcts/games/risk/strategies:risk_proposer",
        "//game_mcts/games/risk/strategies:risk_rollout_shortcuts",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/flags:parse",
        "@protobuf//:protobuf",
    ],
)

cc_binary(
    name = "risk_tournament",
    srcs = ["risk_tournament.cpp"],
    deps = [
        ":risk_cc_proto",
        ":risk_game",
        ":risk_serialization",
        "//game_mcts/core/mcts:mcts",
        "//game_mcts/core/mcts:tournament",
        "//game_mcts/games/risk/strategies:risk_proposer",
        "//game_mcts/games/risk/strategies:risk_rollout_shortcuts",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/flags:parse",
        "@protobuf//:protobuf",
    ],
)

cc_library(
    name = "risk_game_state_sampling",
    hdrs = ["risk_game_state_sampling.h"],
    deps = [
        ":risk_game",
        "//game_mcts/core/mcts:game_traits",
        "//game_mcts/core/util:overloaded",
        "//game_mcts/core/util:state_space",
        "//game_mcts/games/risk/strategies:expected_battle_outcomes",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
    ],
)

cc_test(
    name = "risk_game_state_sampling_test",
    srcs = ["risk_game_state_sampling_test.cpp"],
    deps = [
        ":risk_board",
        ":risk_game",
        ":risk_game_state_sampling",
        "//game_mcts/games/risk/strategies:risk_proposer",
        "@googletest//:gtest_main",
    ],
)

proto_library(
    name = "risk_proto",
    srcs = ["risk.proto"],
)

cc_proto_library(
    name = "risk_cc_proto",
    deps = [":risk_proto"],
)

# Python bindings for risk.proto; the generated module lands at
# bazel-bin/game_mcts/games/risk/risk_pb2.py (see notebooks/risk_trajectory_reader.py).
py_proto_library(
    name = "risk_py_proto",
    visibility = ["//visibility:public"],
    deps = [":risk_proto"],
)

# Opt-in protobuf serialization for RiskState (mcts::GameSerializationTraits).
cc_library(
    name = "risk_serialization",
    srcs = ["risk_serialization.cpp"],
    hdrs = ["risk_serialization.h"],
    deps = [
        ":risk_cc_proto",
        ":risk_game",
        "//game_mcts/core/mcts:serialization",
        "//game_mcts/core/util:overloaded",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
    ],
)

cc_test(
    name = "risk_serialization_test",
    srcs = ["risk_serialization_test.cpp"],
    deps = [
        ":risk_game",
        ":risk_serialization",
        "//game_mcts/games/risk/strategies:risk_proposer",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "risk_mcts_export_test",
    srcs = ["risk_mcts_export_test.cpp"],
    deps = [
        ":risk_game",
        ":risk_serialization",
        "//game_mcts/core/mcts:mcts",
        "//game_mcts/core/mcts:mcts_export",
        "//game_mcts/core/mcts:mcts_tree_cc_proto",
        "//game_mcts/games/risk/strategies:risk_proposer",
        "//game_mcts/games/risk/strategies:risk_rollout_shortcuts",
        "@googletest//:gtest_main",
    ],
)
