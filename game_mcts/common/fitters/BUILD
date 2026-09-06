load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@rules_cc//cc:cc_test.bzl", "cc_test")

package(
    default_visibility = ["//visibility:public"],
)

cc_library(
    name = "simple_fitters",
    srcs = [
        "simple_fitters.cpp",
    ],
    hdrs = [
        "simple_fitters.h",
    ],
    deps = [
    ],
)

cc_test(
    name = "simple_fitters_test",
    srcs = [
        "simple_fitters_test.cpp",
    ],
    deps = [
        ":simple_fitters",
        "@googletest//:gtest_main",
    ],
)
