// The harness every submission is compiled into, with CANDIDATE_ENTRY_HEADER
// naming the submitted header (submission.harness in problem.textproto).
//
//   bazel run //bots/reference:bot --
//       --name=reference --server=localhost:50051
//       --opponent=builtin:mcts,builtin:random --games=6
//       --params=iterations=800

#include <grpcpp/grpcpp.h>

#include <cstdint>
#include <random>
#include <string>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/log/globals.h"
#include "absl/log/initialize.h"
#include "absl/log/log.h"
#include "bots/bot_api.h"
#include "game_arena/client/play_loop.h"
#include "game_arena/proto/tournament_broker.grpc.pb.h"
#include "game_mcts/core/mcts/policies.h"
#include "game_mcts/games/risk/risk_serialization.h"

#ifndef CANDIDATE_ENTRY_HEADER
#error "compile with -DCANDIDATE_ENTRY_HEADER=\"path/to/strategy.h\""
#endif
#include CANDIDATE_ENTRY_HEADER

ABSL_FLAG(std::string, server, "localhost:50051", "host:port of the referee");
ABSL_FLAG(std::string, name, "", "Player name (required)");
ABSL_FLAG(std::string, opponent, "builtin:random,builtin:random",
          "Every other seat, comma-separated, each builtin:random | "
          "builtin:mcts[:iterations=N] | player:<name>");
ABSL_FLAG(int, games, 1, "Number of games to play");
ABSL_FLAG(std::string, params, "",
          "Tuning knobs for MakePolicy, as key=value,key=value");
ABSL_FLAG(int, seed, 0, "RNG seed; 0 draws from the system entropy source");

int main(int argc, char **argv) {
  absl::ParseCommandLine(argc, argv);
  absl::InitializeLog();
  // Otherwise the per-game result lines never reach a submitter's terminal.
  absl::SetStderrThreshold(absl::LogSeverityAtLeast::kInfo);

  const std::string name = absl::GetFlag(FLAGS_name);
  if (name.empty()) {
    LOG(ERROR) << "Missing required --name=<player name>";
    return 1;
  }

  const int seed = absl::GetFlag(FLAGS_seed);
  std::mt19937 gen(seed != 0 ? static_cast<uint32_t>(seed)
                             : std::random_device{}());

  auto channel = grpc::CreateChannel(absl::GetFlag(FLAGS_server),
                                     grpc::InsecureChannelCredentials());
  auto stub = tournament_broker::proto::TournamentBroker::NewStub(channel);

  // Built once: a submission may spend real work here that must not be
  // repeated per game.
  const auto choose = mcts::tournament::SerializedPolicy<candidate::game_t>(
      MakePolicy(candidate::Params::Parse(absl::GetFlag(FLAGS_params))));
  return tournament_client::PlayGames(stub.get(), name,
                                      std::string(candidate::kGameName),
                                      absl::GetFlag(FLAGS_opponent),
                                      absl::GetFlag(FLAGS_games), choose, gen)
             ? 0
             : 1;
}
