// Replays recorded risk2 games (the referee's GameRecord .pb files) in the
// terminal.
//
//   bazel run //problem:risk_replay -- ~/.arena/risk2/games/*.pb | less -R
//   bazel run //problem:risk_replay -- --mode=full --play --delay_ms=300 g.pb
//   bazel run //problem:risk_replay -- --stats ~/.arena/risk2/games/*.pb
//
// --mode=arena prints what the referee records for the dashboard: every
// step's caption, and the board when the step draws one. --mode=full draws
// the board, marked, on every step. --stats prints only the bytes a game's
// captions and views take, by kind.

#include <chrono>
#include <cstddef>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "game_arena/proto/tournament_broker.pb.h"
#include "game_mcts/core/mcts/serialization.h"
#include "game_mcts/games/risk/risk_game.h"
#include "game_mcts/games/risk/risk_render.h"
#include "game_mcts/games/risk/risk_serialization.h"
#include "problem/risk_session.h"

ABSL_FLAG(std::string, mode, "arena", "arena | full");
ABSL_FLAG(bool, stats, false, "Print only view bytes per game, by kind");
ABSL_FLAG(bool, play, false, "Animate: redraw each step in place");
ABSL_FLAG(int, delay_ms, 400, "With --play, the time per step");
ABSL_FLAG(int, max_rounds, 40, "The round cap the game was played with");

namespace {

using state_t = tournament_broker::RiskSession::state_t;
using traits = mcts::GameSerializationTraits<state_t>;

struct Stats {
  std::size_t captions_ = 0;
  std::size_t full_ = 0;
  std::size_t full_bytes_ = 0;
  std::size_t bands_ = 0;
  std::size_t band_bytes_ = 0;
};

void Show(const std::string &text) {
  if (absl::GetFlag(FLAGS_play)) {
    std::cout << "\x1b[H\x1b[2J" << text << std::flush;
    std::this_thread::sleep_for(
        std::chrono::milliseconds(absl::GetFlag(FLAGS_delay_ms)));
  } else {
    std::cout << text;
  }
}

bool Replay(const std::string &path) {
  tournament_broker::proto::GameRecord record;
  std::ifstream in(path, std::ios::binary);
  if (!record.ParseFromIstream(&in)) {
    std::cerr << path << ": not a GameRecord\n";
    return false;
  }
  traits::state_proto_t initial;
  if (!initial.ParseFromString(record.initial_state())) {
    std::cerr << path << ": initial state is not a risk2 state\n";
    return false;
  }
  tournament_broker::RiskSession session(absl::GetFlag(FLAGS_max_rounds),
                                         traits::StateFromProto(initial));
  const bool full = absl::GetFlag(FLAGS_mode) == "full";
  const bool stats = absl::GetFlag(FLAGS_stats);
  std::string title = record.game_id() + ": ";
  for (int seat = 0; seat < record.player_names_size(); ++seat) {
    title += (seat > 0 ? " vs " : "") + std::string("P") +
             std::to_string(seat) + " " + record.player_names(seat);
  }
  title += "  (" + record.termination_reason() + ")\n";

  std::string view = session.RenderState();
  Stats counts{.full_ = 1, .full_bytes_ = view.size()};
  if (!stats) {
    Show(title + view);
  }
  for (int i = 0; i < record.steps_size(); ++i) {
    const state_t before = session.State();
    std::string error;
    if (!session.ApplySerializedAction(record.steps(i).action(), &error)) {
      std::cerr << path << ": step " << i + 1 << ": " << error << "\n";
      return false;
    }
    const std::string caption = session.RenderLastStep();
    const std::string step_view = session.RenderState();
    counts.captions_ += caption.size();
    if (!step_view.empty()) {
      const bool turn_board = step_view.starts_with("Round ");
      (turn_board ? counts.full_ : counts.bands_) += 1;
      (turn_board ? counts.full_bytes_ : counts.band_bytes_) +=
          step_view.size();
    }
    if (stats) {
      continue;
    }
    if (full) {
      traits::action_proto_t proto;
      proto.ParseFromString(record.steps(i).action());
      view = risk_game::RenderBoard(
          session.State(),
          risk_game::StepMarks(before, traits::ActionFromProto(proto)),
          risk_game::BoardDetail::kFull);
    } else if (!step_view.empty() || !absl::GetFlag(FLAGS_play)) {
      view = step_view;
    }
    Show("Move " + std::to_string(i + 1) + ": " + caption + "\n" + view);
  }
  if (stats) {
    std::printf(
        "%s %d steps: captions %zu KB, %zu turn boards %zu KB, %zu bands %zu "
        "KB, total %zu KB\n",
        record.game_id().c_str(), record.steps_size(), counts.captions_ / 1024,
        counts.full_, counts.full_bytes_ / 1024, counts.bands_,
        counts.band_bytes_ / 1024,
        (counts.captions_ + counts.full_bytes_ + counts.band_bytes_) / 1024);
  }
  return true;
}

}  // namespace

int main(int argc, char **argv) {
  const std::vector<char *> paths = absl::ParseCommandLine(argc, argv);
  if (paths.size() < 2) {
    std::cerr << "usage: risk_replay [--mode=arena|full] [--stats] [--play] "
                 "GAME.pb...\n";
    return 2;
  }
  bool ok = true;
  for (std::size_t i = 1; i < paths.size(); ++i) {
    ok = Replay(paths[i]) && ok;
  }
  return ok ? 0 : 1;
}
