load("@rules_cc//cc:cc_binary.bzl", "cc_binary")

package(default_visibility = ["//visibility:public"])

# Benchmark and demo binaries. These sit at the top of the dependency graph:
# they may depend on the framework, the games and the tournament server;
# nothing may depend on them.

cc_binary(
    name = "mcts_bench",
    srcs = ["mcts_bench.cpp"],
    deps = [
        "//game_mcts/core/mcts",
        "//game_mcts/games/tictactoe",
        "@google_benchmark//:benchmark_main",
    ],
)

cc_binary(
    name = "mcts_convergence_speed",
    srcs = ["mcts_convergence_speed.cpp"],
    deps = [
        "//game_mcts/common/fitters:simple_fitters",
        "//game_mcts/core/mcts",
        "//game_mcts/games/tictactoe",
        "//game_mcts/tools/viz:visualisations",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/flags:parse",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:initialize",
    ],
)

cc_binary(
    name = "risk_game_benchmark",
    srcs = ["risk_game_benchmark.cpp"],
    deps = [
        "//game_mcts/core/mcts:mcts",
        "//game_mcts/games/risk:risk_game",
        "//game_mcts/games/risk/strategies:risk_proposer",
        "//game_mcts/games/risk/strategies:risk_rollout_shortcuts",
        "@google_benchmark//:benchmark_main",
    ],
)

# In-process load driver: move throughput vs concurrent clients. Compiles
# against the pre-refactor broker too, so the two concurrency models can be
# measured with the same harness.
cc_binary(
    name = "throughput_benchmark",
    srcs = ["throughput_benchmark.cc"],
    deps = [
        "//game_mcts/tournament_server/benchgame:bench_game",
        "//game_mcts/tournament_server/benchgame:bench_serialization",
        "//game_mcts/tournament_server/proto:tournament_broker_cc_grpc",
        "//game_mcts/tournament_server/referee:broker_service",
        "//game_mcts/tournament_server/server:elo_store",
        "//game_mcts/tournament_server/server:game_history",
        "//game_mcts/tournament_server/referee:matchmaker",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/flags:parse",
        "@abseil-cpp//absl/log:globals",
        "@abseil-cpp//absl/log:initialize",
        "@grpc//:grpc++",
    ],
)
