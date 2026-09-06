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
