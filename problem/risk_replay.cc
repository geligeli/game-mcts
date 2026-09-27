// Replays recorded risk2 games (the referee's GameRecord .pb files) in the
// terminal.
//
//   bazel run //problem:risk_replay -- ~/.arena/risk2/games/*.pb | less -R
//   bazel run //problem:risk_replay -- --mode=full --play --delay_ms=300 g.pb
//   bazel run //problem:risk_replay -- --stats ~/.arena/risk2/games/*.pb
//
// Every step's caption, the same the dashboard shows, with the ANSI board
// drawn at each turn's start (--mode=turns) or on every step, marked
// (--mode=full). --stats prints only the bytes a game's captions and views
// (the JSON the browser replay draws) take.

#include <algorithm>
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

ABSL_FLAG(std::string, mode, "turns", "turns | full");
ABSL_FLAG(bool, stats, false, "Print only caption and view bytes per game");
ABSL_FLAG(bool, play, false, "Animate: redraw each step in place");
ABSL_FLAG(int, delay_ms, 400, "With --play, the time per step");
ABSL_FLAG(int, max_rounds, 40, "The round cap the game was played with");

namespace {

using state_t = tournament_broker::RiskSession::state_t;
using traits = mcts::GameSerializationTraits<state_t>;

struct Stats {
  std::size_t captions_ = 0;
  std::size_t views_ = 0;
  std::size_t largest_view_ = 0;
};

// The step taken from |before| was a player's first of their turn.
bool TurnStart(const state_t &before) {
  return !before.initial_placement_ && before.current_player_ >= 0 &&
         !before.queued_attack_.has_value() && before.first_attack_of_turn_ &&
         before.reserves_[before.current_player_] > 0;
}

std::string Board(const state_t &state, const risk_game::BoardMarks &marks) {
  return "Round " +
         std::to_string(tournament_broker::RiskSession::Rounds(state)) + "\n" +
         risk_game::RenderBoard(state, marks);
}

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

  Stats counts{.views_ = session.RenderState().size()};
  if (!stats) {
    Show(title + Board(session.State(), {}));
  }
  for (int i = 0; i < record.steps_size(); ++i) {
    const state_t before = session.State();
    std::string error;
    if (!session.ApplySerializedAction(record.steps(i).action(), &error)) {
      std::cerr << path << ": step " << i + 1 << ": " << error << "\n";
      return false;
    }
    const std::string caption = session.RenderLastStep();
    const std::size_t view = session.RenderState().size();
    counts.captions_ += caption.size();
    counts.views_ += view;
    counts.largest_view_ = std::max(counts.largest_view_, view);
    if (stats) {
      continue;
    }
    std::string text = "Move " + std::to_string(i + 1) + ": " + caption + "\n";
    if (full || TurnStart(before)) {
      traits::action_proto_t proto;
      proto.ParseFromString(record.steps(i).action());
      text +=
          Board(session.State(),
                risk_game::StepMarks(before, traits::ActionFromProto(proto)));
    }
    Show(text);
  }
  if (stats) {
    std::printf(
        "%s %d steps: captions %zu KB, views %zu KB (largest %zu B), total "
        "%zu KB\n",
        record.game_id().c_str(), record.steps_size(), counts.captions_ / 1024,
        counts.views_ / 1024, counts.largest_view_,
        (counts.captions_ + counts.views_) / 1024);
  }
  return true;
}

}  // namespace

int main(int argc, char **argv) {
  const std::vector<char *> paths = absl::ParseCommandLine(argc, argv);
  if (paths.size() < 2) {
    std::cerr << "usage: risk_replay [--mode=turns|full] [--stats] [--play] "
                 "GAME.pb...\n";
    return 2;
  }
  bool ok = true;
  for (std::size_t i = 1; i < paths.size(); ++i) {
    ok = Replay(paths[i]) && ok;
  }
  return ok ? 0 : 1;
}
