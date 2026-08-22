load("@rules_python//python:defs.bzl", "py_library")

filegroup(
    name = "template_files",
    srcs = glob([
        "templates/*.txt",
        "templates/*.npz.npy",
    ]),
    visibility = ["//game_mcts/cpp/risk:__pkg__"],
)

exports_files(
    ["template_to_cpp.py"],
    visibility = ["//game_mcts/cpp/risk:__pkg__"],
)

py_library(
    name = "mhmc",
    srcs = ["mhmc.py"],
)

py_library(
    name = "glyphs",
    srcs = ["glyphs.py"],
)

py_library(
    name = "risk_board_polygons",
    srcs = ["risk_board_polygons.py"],
)
