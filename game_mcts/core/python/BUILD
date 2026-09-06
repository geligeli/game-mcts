load("@pybind11_bazel//:build_defs.bzl", "pybind_library")
load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@rules_cc//cc:cc_test.bzl", "cc_test")

package(
    default_visibility = ["//visibility:public"],
)

# Type-erased driving interface (mcts::PyGame) over any ProtoSerializableGame:
# proto messages cross as serialized bytes, so a recorded game can be replayed
# (or driven live) through one game-agnostic API. Pybind-free; the pybind11
# glue is :py_game_binding.
cc_library(
    name = "py_game",
    hdrs = ["py_game.h"],
    deps = ["//game_mcts/core/mcts:serialization"],
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
        "//game_mcts/games/tictactoe:tictactoe_cc_proto",
        "//game_mcts/games/tictactoe:tictactoe_serialization",
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
        ":py_game",
        "//game_mcts/core/mcts:mcts",
        "//game_mcts/core/mcts:mcts_export",
        "//game_mcts/core/mcts:serialization",
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
        ":py_mcts",
        "//game_mcts/core/mcts:mcts_tree_cc_proto",
        "//game_mcts/games/tictactoe:tictactoe_cc_proto",
        "//game_mcts/games/tictactoe:tictactoe_serialization",
        "@googletest//:gtest_main",
    ],
)
