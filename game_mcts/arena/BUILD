load("@rules_cc//cc:cc_binary.bzl", "cc_binary")
load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@rules_cc//cc:cc_test.bzl", "cc_test")

package(
    default_visibility = ["//visibility:public"],
)

# Everything that binds this framework to the arena.
#
# The arena (//game_mcts/tournament_server) is game-agnostic: it speaks
# serialized states and actions and gets its rules from a GameRegistry it only
# declares. This package supplies that registry for the game_mcts games, plus
# the adapter that lets any mcts::SerializableGame be one. Nothing here is
# needed to run an arena -- it is needed to run *these games* in one.

# The adapter: any mcts::SerializableGame as an arena GameSession.
cc_library(
    name = "game_session_impl",
    hdrs = ["game_session_impl.h"],
    deps = [
        "//game_mcts/core/mcts:game_traits",
        "//game_mcts/core/mcts:serialization",
        "//game_mcts/tournament_server/referee:game_session",
        "@abseil-cpp//absl/log:check",
    ],
)

# Game-generic built-in strategies (random, minimax), written against the mcts
# concepts. Game-specific entries live in game_registry.cc.
cc_library(
    name = "builtins",
    hdrs = ["builtins.h"],
    deps = [
        ":game_session_impl",
        "//game_mcts/core/mcts:game_traits",
        "//game_mcts/core/mcts:minimax",
        "//game_mcts/core/mcts:serialization",
        "//game_mcts/tournament_server/referee:game_session",
    ],
)

# The definition of GameRegistry() that the arena only declares. This is the
# one place game code enters the tournament system. alwayslink because nothing
# depends on it by label; it exists to satisfy an undefined symbol.
cc_library(
    name = "game_registry",
    srcs = ["game_registry.cc"],
    alwayslink = 1,
    deps = [
        ":builtins",
        ":game_session_impl",
        "//game_mcts/arena/benchgame:bench_game",
        "//game_mcts/arena/benchgame:bench_serialization",
        "//game_mcts/core/mcts:game_traits",
        "//game_mcts/core/mcts:mcts",
        "//game_mcts/games/risk:risk_game",
        "//game_mcts/games/risk:risk_serialization",
        "//game_mcts/games/risk/strategies:risk_proposer",
        "//game_mcts/games/risk/strategies:risk_rollout_shortcuts",
        "//game_mcts/games/tictactoe:tictactoe",
        "//game_mcts/games/tictactoe:tictactoe_serialization",
        "//game_mcts/tournament_server/referee:game_registry",
    ],
)

# The arena's entry points, linked against this repo's games. Each is a
# registry plus a game-agnostic main(); see the arena's referee package.
cc_binary(
    name = "match_referee",
    deps = [
        ":game_registry",
        "//game_mcts/tournament_server/referee:referee_main",
    ],
)

cc_binary(
    name = "broker_server",
    deps = [
        ":game_registry",
        "//game_mcts/tournament_server/referee:broker_server_main",
    ],
)

cc_binary(
    name = "random_client",
    deps = [
        ":game_registry",
        "//game_mcts/tournament_server/client:random_client_main",
    ],
)

cc_test(
    name = "shipped_problems_test",
    srcs = ["shipped_problems_test.cc"],
    data = ["//game_mcts/arena/problems:configs"],
    deps = [
        "//game_mcts/tournament_server/server:problem_config",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "game_session_test",
    srcs = ["game_session_test.cc"],
    deps = [
        ":game_registry",
        ":game_session_impl",
        "//game_mcts/games/risk:risk_game",
        "//game_mcts/games/risk:risk_serialization",
        "//game_mcts/games/risk/strategies:risk_proposer",
        "//game_mcts/games/tictactoe:tictactoe",
        "//game_mcts/games/tictactoe:tictactoe_cc_proto",
        "//game_mcts/games/tictactoe:tictactoe_serialization",
        "//game_mcts/tournament_server/referee:game_registry",
        "@googletest//:gtest_main",
    ],
)

# The arena's own coverage of these paths runs on its own game; this is the
# same ground with a real one underneath.
cc_test(
    name = "game_run_test",
    srcs = ["game_run_test.cc"],
    deps = [
        ":game_registry",
        "//game_mcts/games/tictactoe:tictactoe",
        "//game_mcts/games/tictactoe:tictactoe_cc_proto",
        "//game_mcts/games/tictactoe:tictactoe_serialization",
        "//game_mcts/tournament_server/referee:client_handle",
        "//game_mcts/tournament_server/referee:game_registry",
        "//game_mcts/tournament_server/referee:game_run",
        "//game_mcts/tournament_server/referee:worker_pool",
        "//game_mcts/tournament_server/server:elo_store",
        "//game_mcts/tournament_server/server:game_history",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "broker_integration_test",
    srcs = ["broker_integration_test.cc"],
    deps = [
        ":game_registry",
        "//game_mcts/arena/client:remote_client",
        "//game_mcts/games/tictactoe:tictactoe",
        "//game_mcts/games/tictactoe:tictactoe_serialization",
        "//game_mcts/tournament_server/proto:tournament_broker_cc_grpc",
        "//game_mcts/tournament_server/referee:broker_service",
        "//game_mcts/tournament_server/referee:client_handle",
        "//game_mcts/tournament_server/referee:game_registry",
        "//game_mcts/tournament_server/referee:matchmaker",
        "//game_mcts/tournament_server/server:elo_standings",
        "//game_mcts/tournament_server/server:elo_store",
        "//game_mcts/tournament_server/server:game_history",
        "//game_mcts/tournament_server/server:http_leaderboard",
        "@googletest//:gtest_main",
        "@grpc//:grpc++",
    ],
)
