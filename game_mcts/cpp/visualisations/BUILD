load("@rules_cc//cc:cc_binary.bzl", "cc_binary")
load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@rules_cc//cc:cc_test.bzl", "cc_test")

cc_library(
    name = "http_server",
    srcs = [
        "http_server.cpp",
    ],
    hdrs = [
        "http_server.h",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "@abseil-cpp//absl/log",
    ],
)

cc_library(
    name = "visualisations",
    srcs = [
        "visualisations.cpp",
    ],
    hdrs = [
        "visualisations.h",
    ],
    visibility = ["//visibility:public"],
    deps = [
        ":http_server",
    ],
)

cc_test(
    name = "visualisations_test",
    srcs = ["visualisations_test.cpp"],
    deps = [
        ":visualisations",
        "@googletest//:gtest_main",
    ],
)

cc_binary(
    name = "http_server_demo",
    srcs = ["http_server_demo.cpp"],
    deps = [
        ":visualisations",
    ],
)

cc_binary(
    name = "comprehensive_demo",
    srcs = ["comprehensive_demo.cpp"],
    deps = [
        ":visualisations",
    ],
)
