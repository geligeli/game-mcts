// Streams MCTS self-play games of Risk to the terminal using the ascii board
// rendering. Runs until aborted (Ctrl-C). Example:
//   bazel run //game_mcts/cpp/risk:risk_mcts_selfplay --
//       --iterations_per_move=100 --widening_alpha=0.25 --delay_ms=200
//
// Per-player overrides and game recording:
//   bazel run //game_mcts/cpp/risk:risk_mcts_selfplay -- --record_games=/tmp/games
//       "--player_params=iterations=400,rollout=exact;"
//
// Interactive keys: space pauses/resumes; while paused 's' dumps the current
// state as a protobuf-text RiskState (state_game<g>_move<m>.pbtxt) and 'a'
// dumps the ascii board (board_game<g>_move<m>.txt) into the working
// directory; 'q' quits.

#include <fcntl.h>
#include <signal.h>
#include <termios.h>
#include <unistd.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <optional>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "game_mcts/cpp/risk/risk.pb.h"
#include "game_mcts/cpp/risk/risk_game.h"
#include "game_mcts/cpp/risk/risk_serialization.h"
#include "game_mcts/cpp/risk/strategies/risk_proposer.h"
#include "game_mcts/cpp/risk/strategies/risk_rollout_shortcuts.h"
#include "game_mcts/cpp/mcts/mcts.inl"
#include "google/protobuf/text_format.h"

ABSL_FLAG(int, iterations_per_move, 50, "MCTS iterations per decision node");
ABSL_FLAG(double, widening_c, 2.0,
          "Progressive widening coefficient: expand while "
          "children < widening_c * visits^widening_alpha");
ABSL_FLAG(double, widening_alpha, 0.5, "Progressive widening exponent");
ABSL_FLAG(int, seed, -1, "RNG seed (-1 = random)");
ABSL_FLAG(int, max_moves_per_game, 1000,
          "Safety cap on moves per game before declaring a draw");
ABSL_FLAG(int, delay_ms, 0, "Pause between moves (milliseconds)");
ABSL_FLAG(bool, highlight_changes, true,
          "Highlight countries whose army count changed since the previous "
          "frame (bright background, regardless of owner)");
ABSL_FLAG(bool, exact_rollouts, false,
          "Use exact dice sampling in MCTS rollouts instead of resolving "
          "battles with the expected-outcome table");
ABSL_FLAG(int, num_players, 2, "Number of players (2-6)");
ABSL_FLAG(std::string, player_params, "",
          "Per-player MCTS parameter overrides, one comma-separated "
          "key=value list per player, players separated by ';' (empty "
          "segments = use the global flags). Keys: iterations, widening_c, "
          "widening_alpha, rollout (exact | expected). Example: "
          "--player_params='iterations=400,rollout=exact;widening_alpha=0.25'");
ABSL_FLAG(std::string, record_games, "",
          "Record every game as a protobuf-text RiskTrajectory "
          "(game_<index>.pbtxt) into this directory, which is created if "
          "missing. The trajectory is kept in memory during play and flushed "
          "(atomically, via a .tmp rename) whenever the game is paused with "
          "space, plus a final write with the result when the game completes. "
          "Empty = disabled. Self-play runs until Ctrl-C, so recording keeps "
          "writing files (a few MB each) until aborted.");

namespace {

volatile sig_atomic_t g_abort = 0;

void OnSigInt(int) { g_abort = 1; }

// Writes the full buffer to stdout, tolerating short writes.
void WriteAll(std::string_view data) {
  while (!data.empty()) {
    const ssize_t n = write(STDOUT_FILENO, data.data(), data.size());
    if (n <= 0) {
      break;
    }
    data.remove_prefix(static_cast<size_t>(n));
  }
}

// RAII guard for the alternate screen buffer; restores the terminal on exit.
struct ScreenGuard {
  ScreenGuard() { WriteAll("\033[?1049h\033[?25l"); }
  ~ScreenGuard() { WriteAll("\033[?25h\033[?1049l"); }
  ScreenGuard(const ScreenGuard &) = delete;
  auto operator=(const ScreenGuard &) -> ScreenGuard & = delete;
};

// RAII guard for raw keyboard input: switches stdin to non-canonical,
// no-echo, non-blocking mode so the game loop can poll single keys without
// line buffering. The non-blocking flag is set even when stdin is not a
// terminal (e.g. piped input), so polling never blocks the game loop.
struct TerminalInputGuard {
  TerminalInputGuard() {
    termios_valid_ = tcgetattr(STDIN_FILENO, &saved_termios_) == 0;
    if (termios_valid_) {
      struct termios raw = saved_termios_;
      raw.c_lflag &= ~(ICANON | ECHO);
      raw.c_cc[VMIN] = 0;
      raw.c_cc[VTIME] = 0;
      termios_valid_ = tcsetattr(STDIN_FILENO, TCSANOW, &raw) == 0;
    }
    saved_flags_ = fcntl(STDIN_FILENO, F_GETFL);
    flags_valid_ = saved_flags_ != -1 && fcntl(STDIN_FILENO, F_SETFL,
                                               saved_flags_ | O_NONBLOCK) != -1;
  }
  ~TerminalInputGuard() {
    if (termios_valid_) {
      tcsetattr(STDIN_FILENO, TCSANOW, &saved_termios_);
    }
    if (flags_valid_) {
      fcntl(STDIN_FILENO, F_SETFL, saved_flags_);
    }
  }
  TerminalInputGuard(const TerminalInputGuard &) = delete;
  auto operator=(const TerminalInputGuard &) -> TerminalInputGuard & = delete;

 private:
  struct termios saved_termios_ {};
  int saved_flags_ = -1;
  bool termios_valid_ = false;
  bool flags_valid_ = false;
};

// Returns the next pending keypress, or nullopt if no key is buffered.
auto PollKey() -> std::optional<char> {
  char c;
  if (read(STDIN_FILENO, &c, 1) == 1) {
    return c;
  }
  return std::nullopt;
}

struct Stats {
  int games = 0;
  std::array<int, 6> wins{};  // Only the first num_players entries are used.
  int draws = 0;
  int total_moves = 0;
  double total_think_ms = 0.0;
  int think_count = 0;
};

// Army counts shown in the previous frame, used to highlight changes.
struct FrameHistory {
  std::array<int, risk_game::kNumTerritories> units{};
  bool valid = false;
};

// MCTS parameters of one player: defaults come from the global flags, the
// --player_params entries override them per player.
struct PlayerParams {
  int iterations;
  double widening_c;
  double widening_alpha;
  bool exact_rollouts;
};

auto GlobalPlayerParams() -> PlayerParams {
  return {.iterations = std::max(1, absl::GetFlag(FLAGS_iterations_per_move)),
          .widening_c = absl::GetFlag(FLAGS_widening_c),
          .widening_alpha = absl::GetFlag(FLAGS_widening_alpha),
          .exact_rollouts = absl::GetFlag(FLAGS_exact_rollouts)};
}

auto Trim(const std::string &s) -> std::string {
  const auto first = s.find_first_not_of(" \t\r\n");
  if (first == std::string::npos) {
    return "";
  }
  return s.substr(first, s.find_last_not_of(" \t\r\n") - first + 1);
}

// Applies one comma-separated key=value --player_params entry on top of
// |params|. Throws std::runtime_error on unknown keys or malformed values.
auto ParsePlayerParams(const std::string &spec,
                       PlayerParams params) -> PlayerParams {
  size_t pos = 0;
  while (pos <= spec.size()) {
    const size_t comma = spec.find(',', pos);
    const std::string token = Trim(spec.substr(
        pos, comma == std::string::npos ? std::string::npos : comma - pos));
    if (!token.empty()) {
      const auto eq = token.find('=');
      if (eq == std::string::npos) {
        throw std::runtime_error("expected 'key = value', got '" + token + "'");
      }
      const std::string key = Trim(token.substr(0, eq));
      const std::string value = Trim(token.substr(eq + 1));
      try {
        if (key == "iterations") {
          params.iterations = std::max(1, std::stoi(value));
        } else if (key == "widening_c") {
          params.widening_c = std::stod(value);
        } else if (key == "widening_alpha") {
          params.widening_alpha = std::stod(value);
        } else if (key == "rollout") {
          if (value == "exact") {
            params.exact_rollouts = true;
          } else if (value == "expected") {
            params.exact_rollouts = false;
          } else {
            throw std::runtime_error("expected: exact | expected");
          }
        } else {
          throw std::runtime_error("unknown key '" + key +
                                   "' (expected: iterations | widening_c | "
                                   "widening_alpha | rollout)");
        }
      } catch (const std::invalid_argument &) {
        throw std::runtime_error("bad value for '" + key + "': " + value);
      } catch (const std::out_of_range &) {
        throw std::runtime_error("bad value for '" + key + "': " + value);
      }
    }
    if (comma == std::string::npos) {
      break;
    }
    pos = comma + 1;
  }
  return params;
}

// Rollout policy with a runtime exact/expected-battles switch, so each
// player can pick its own rollout mode.
template <size_t NUM_PLAYERS>
struct SwitchableRollout {
  bool exact_rollouts;
  using game_t = risk_game::RiskState<NUM_PLAYERS>;
  using proposer_t = risk_game::RiskProposer<NUM_PLAYERS>;

  auto operator()(game_t game, std::mt19937 &gen) const {
    if (exact_rollouts) {
      return mcts::RandomRollout<game_t, proposer_t>{}(std::move(game), gen);
    }
    return mcts::MakeShortcutRollout<game_t, proposer_t>(
        &risk_game::ResolveBattleWithExpectationInPlace<NUM_PLAYERS>)(
        std::move(game), gen);
  }
};

// Renders one frame: status lines on top, ascii board below. Homes the cursor
// and clears below instead of clearing the screen to avoid flicker. Countries
// whose army count changed since the previous frame are highlighted with a
// bright background (unless --nohighlight_changes). Updates |history|.
template <size_t NUM_PLAYERS>
void Render(const risk_game::RiskState<NUM_PLAYERS> &game, const Stats &stats,
            int move, double last_think_ms, FrameHistory &history,
            const std::vector<PlayerParams> &params,
            const std::string &extra_status = "") {
  std::ostringstream frame;
  frame << "\033[H";

  const int avg_game_length =
      stats.games > 0 ? stats.total_moves / stats.games : 0;
  const double avg_think_ms =
      stats.think_count > 0 ? stats.total_think_ms / stats.think_count : 0.0;

  char status[512];
  const int player = game.current_player();
  if (player >= 0) {
    const PlayerParams &p = params[static_cast<size_t>(player)];
    snprintf(status, sizeof(status),
             "Risk MCTS self-play | game %d | move %d | iters/move=%d | "
             "widening c=%.2f alpha=%.2f | rollouts=%s",
             stats.games + 1, move, p.iterations, p.widening_c,
             p.widening_alpha, p.exact_rollouts ? "exact" : "expected-battles");
  } else {
    snprintf(status, sizeof(status),
             "Risk MCTS self-play | game %d | move %d | chance node (dice)",
             stats.games + 1, move);
  }
  frame << status << "\033[K\r\n";

  frame << "wins:";
  for (size_t p = 0; p < NUM_PLAYERS; ++p) {
    frame << " " << risk_game::PlayerColor(static_cast<int>(p)) << "P" << p
          << risk_game::kColorReset << "=" << stats.wins[p];
  }
  snprintf(status, sizeof(status),
           " draws=%d | avg game %d moves | think: last "
           "%.0f ms avg %.0f ms |%s space = pause | Ctrl-C to quit",
           stats.draws, avg_game_length, last_think_ms, avg_think_ms,
           absl::GetFlag(FLAGS_highlight_changes) ? " bright = armies changed |"
                                                  : "");
  frame << status << "\033[K\r\n";
  if (!extra_status.empty()) {
    frame << extra_status << "\033[K\r\n";
  }

  std::array<bool, risk_game::kNumTerritories> changed{};
  if (history.valid && absl::GetFlag(FLAGS_highlight_changes)) {
    for (size_t i = 0; i < changed.size(); ++i) {
      changed[i] = game.m_map[i].units != history.units[i];
    }
  }
  for (size_t i = 0; i < changed.size(); ++i) {
    history.units[i] = game.m_map[i].units;
  }
  history.valid = true;

  // Turn/Player/Reserves lines + ascii board, changes brightly highlighted.
  // Lines vary in length (e.g. "Player: -1" at chance nodes vs "Player: 0"),
  // so clear to end of line to avoid stale characters from previous frames.
  std::string board = risk_game::RenderRiskState(game, changed);
  for (size_t pos = 0; (pos = board.find('\n', pos)) != std::string::npos;) {
    board.insert(pos, "\033[0m\033[K");
    pos += 8;  // Skip past the 7 inserted escape chars and the newline.
  }
  frame << board;
  frame << "\033[J";

  const std::string out = frame.str();
  WriteAll(out);
}

// Dumps the current game state as a protobuf-text RiskState into
// state_game<index>_move<move>.pbtxt in the working directory. Returns a
// short notice for the pause banner.
template <size_t NUM_PLAYERS>
auto DumpStateProto(const risk_game::RiskState<NUM_PLAYERS> &game,
                    int game_index, int move) -> std::string {
  using traits_t =
      mcts::GameSerializationTraits<risk_game::RiskState<NUM_PLAYERS>>;
  const std::string filename = "state_game" + std::to_string(game_index) +
                               "_move" + std::to_string(move) + ".pbtxt";
  std::string text;
  if (!google::protobuf::TextFormat::PrintToString(traits_t::StateToProto(game),
                                                   &text)) {
    return "state dump failed (TextFormat)";
  }
  if (std::ofstream out(filename); out) {
    out << text;
  } else {
    return "cannot open " + filename;
  }
  return "state written to " + filename;
}

// Dumps the current ascii board (with ANSI colors) into
// board_game<index>_move<move>.txt. Returns a short notice for the banner.
template <size_t NUM_PLAYERS>
auto DumpAsciiBoard(const risk_game::RiskState<NUM_PLAYERS> &game,
                    int game_index, int move) -> std::string {
  const std::string filename = "board_game" + std::to_string(game_index) +
                               "_move" + std::to_string(move) + ".txt";
  std::array<bool, risk_game::kNumTerritories> no_highlight{};
  if (std::ofstream out(filename); out) {
    out << risk_game::RenderRiskState(game, no_highlight);
  } else {
    return "cannot open " + filename;
  }
  return "board written to " + filename;
}

// Writes |trajectory| as protobuf text to |path|, atomically (tmp file +
// rename) so interrupted or concurrent readers never see a partial file.
void WriteTrajectory(const std::filesystem::path &path,
                     const risk_game::proto::RiskTrajectory &trajectory) {
  std::string text;
  if (!google::protobuf::TextFormat::PrintToString(trajectory, &text)) {
    fprintf(stderr, "TextFormat failed for %s\n", path.string().c_str());
    return;
  }
  const std::filesystem::path tmp_path = path.string() + ".tmp";
  {
    std::ofstream out(tmp_path);
    if (!out) {
      fprintf(stderr, "Cannot open %s\n", tmp_path.string().c_str());
      return;
    }
    out << text;
  }
  std::error_code ec;
  std::filesystem::rename(tmp_path, path, ec);
  if (ec) {
    fprintf(stderr, "Cannot rename %s to %s: %s\n", tmp_path.string().c_str(),
            path.string().c_str(), ec.message().c_str());
  }
}

// Handles one iteration of keyboard input. Unpaused: space pauses, 'q'
// quits. Entering pause flushes the in-progress trajectory (if recording)
// to |trajectory_path|. Paused: space resumes, 's' dumps the state proto,
// 'a' dumps the ascii board, 'q' quits. Returns false when the program
// should abort.
template <size_t NUM_PLAYERS>
auto HandlePauseInput(const risk_game::RiskState<NUM_PLAYERS> &game,
                      const Stats &stats, int move, double last_think_ms,
                      FrameHistory &history,
                      const std::vector<PlayerParams> &params,
                      risk_game::proto::RiskTrajectory *trajectory,
                      const std::filesystem::path &trajectory_path) -> bool {
  const auto key = PollKey();
  if (!key.has_value()) {
    return true;
  }
  if (*key == 'q') {
    return false;
  }
  if (*key != ' ') {
    return true;
  }
  // Paused: keep re-rendering with a pause banner until space/q. Hotkey
  // results are shown in the banner so the user sees where dumps went.
  std::string notice;
  if (trajectory != nullptr) {
    WriteTrajectory(trajectory_path, *trajectory);
    notice = "trajectory flushed to " + trajectory_path.string();
  }
  while (true) {
    Render(game, stats, move, last_think_ms, history, params,
           std::string("PAUSED | space = resume | s = dump state proto | "
                       "a = dump ascii board | q = quit") +
               (notice.empty() ? "" : " | " + notice));
    const auto pause_key = PollKey();
    if (!pause_key.has_value()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
      continue;
    }
    if (*pause_key == ' ') {
      return true;
    }
    if (*pause_key == 'q') {
      return false;
    }
    if (*pause_key == 's') {
      notice = DumpStateProto(game, stats.games, move);
    } else if (*pause_key == 'a') {
      notice = DumpAsciiBoard(game, stats.games, move);
    }
  }
}

// Plays one move: MCTS at decision nodes, direct sampling at chance nodes.
// The current player's entry in |params| selects the MCTS parameters. If
// |trajectory| is non-null, the applied action is appended to it. Returns
// false if aborted mid-move.
template <size_t NUM_PLAYERS>
auto PlayMove(risk_game::RiskState<NUM_PLAYERS> &game, std::mt19937 &gen,
              Stats &stats, double &last_think_ms,
              const std::vector<PlayerParams> &params,
              risk_game::proto::RiskTrajectory *trajectory) -> bool {
  using game_t = risk_game::RiskState<NUM_PLAYERS>;
  using proposer_t = risk_game::RiskProposer<NUM_PLAYERS>;
  using traits_t = mcts::GameSerializationTraits<game_t>;

  // Records the action before it is applied: current_player() is the deciding
  // player here, or -1 at chance nodes (dice rolls).
  const auto record_action = [&](const risk_game::RiskAction &action) {
    if (trajectory != nullptr) {
      auto *step = trajectory->add_steps();
      step->set_player(game.current_player());
      *step->mutable_action() = traits_t::ActionToProto(action);
    }
  };

  if (game.is_chance_node()) {
    // Dice: rules, not policy.
    const risk_game::RiskAction action = game.sample_chance_action(gen);
    record_action(action);
    game = game.apply_action(action);
    return true;
  }

  const PlayerParams &p = params[static_cast<size_t>(game.current_player())];
  mcts::MctsRunner<game_t, proposer_t, SwitchableRollout<NUM_PLAYERS>> runner(
      game, proposer_t{}, SwitchableRollout<NUM_PLAYERS>{p.exact_rollouts});
  auto picker = mcts::MctsStochasticNodePicker<game_t>(gen, p.widening_c,
                                                       p.widening_alpha);

  const auto start = std::chrono::steady_clock::now();
  for (int i = 0; i < p.iterations && !g_abort; ++i) {
    runner.OneIteration(picker, gen);
  }
  const auto end = std::chrono::steady_clock::now();
  last_think_ms =
      std::chrono::duration<double, std::milli>(end - start).count();
  stats.total_think_ms += last_think_ms;
  ++stats.think_count;

  if (g_abort) {
    return false;
  }
  const risk_game::RiskAction action = runner.best_action();
  record_action(action);
  game = game.apply_action(action);
  return true;
}

// Runs self-play games until aborted. Each game is recorded into
// --record_games as a protobuf-text RiskTrajectory (empty = disabled). The
// trajectory is held in memory during play and flushed to disk when the game
// is paused (space) and once more with the result when the game completes.
template <size_t NUM_PLAYERS>
void RunSelfPlay(std::mt19937 &gen, Stats &stats,
                 const std::vector<PlayerParams> &params,
                 const std::string &record_dir) {
  using game_t = risk_game::RiskState<NUM_PLAYERS>;
  using traits_t = mcts::GameSerializationTraits<game_t>;
  while (!g_abort) {
    const int game_index = stats.games;
    game_t game;
    int move = 0;
    double last_think_ms = 0.0;
    FrameHistory history;  // Fresh game: first frame highlights nothing.

    std::optional<risk_game::proto::RiskTrajectory> trajectory;
    std::filesystem::path trajectory_path;
    if (!record_dir.empty()) {
      trajectory.emplace();
      *trajectory->mutable_initial_state() = traits_t::StateToProto(game);
      trajectory_path = std::filesystem::path(record_dir) /
                        ("game_" + std::to_string(game_index) + ".pbtxt");
    }

    while (!mcts::is_terminal(game.current_state()) &&
           move < absl::GetFlag(FLAGS_max_moves_per_game) && !g_abort) {
      Render(game, stats, move, last_think_ms, history, params);
      if (!HandlePauseInput(game, stats, move, last_think_ms, history, params,
                            trajectory.has_value() ? &*trajectory : nullptr,
                            trajectory_path)) {
        g_abort = 1;  // 'q' pressed.
        break;
      }
      if (const int delay = absl::GetFlag(FLAGS_delay_ms); delay > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(delay));
      }
      if (!PlayMove(game, gen, stats, last_think_ms, params,
                    trajectory.has_value() ? &*trajectory : nullptr)) {
        break;
      }
      ++move;
    }

    if (g_abort) {
      break;
    }
    ++stats.games;
    stats.total_moves += move;
    if (const auto state = game.current_state();
        std::holds_alternative<mcts::win_t>(state)) {
      const int winner = std::get<mcts::win_t>(state).winning_player;
      ++stats.wins[winner];
      if (trajectory.has_value()) {
        trajectory->set_result(risk_game::proto::RiskTrajectory::WIN);
        trajectory->set_winning_player(winner);
      }
    } else {
      ++stats.draws;  // Move cap reached.
      if (trajectory.has_value()) {
        trajectory->set_result(risk_game::proto::RiskTrajectory::DRAW);
      }
    }

    if (trajectory.has_value()) {
      WriteTrajectory(trajectory_path, *trajectory);
    }

    Render(game, stats, move, last_think_ms, history, params);
    std::this_thread::sleep_for(std::chrono::seconds(2));
  }
}

// Instantiates the game loop for the given player count.
template <size_t NUM_PLAYERS>
void RunWithPlayerCount(std::mt19937 &gen, Stats &stats,
                        const std::vector<PlayerParams> &params,
                        const std::string &record_dir) {
  RunSelfPlay<NUM_PLAYERS>(gen, stats, params, record_dir);
}

}  // namespace

auto main(int argc, char **argv) -> int {
  absl::ParseCommandLine(argc, argv);

  const int seed_flag = absl::GetFlag(FLAGS_seed);
  std::mt19937 gen(seed_flag >= 0 ? static_cast<uint32_t>(seed_flag)
                                  : std::random_device{}());

  struct sigaction sa {};
  sa.sa_handler = OnSigInt;
  sa.sa_flags = 0;  // No SA_RESTART: we want blocking calls to interrupt.
  sigaction(SIGINT, &sa, nullptr);

  const int num_players = absl::GetFlag(FLAGS_num_players);
  if (num_players < 2 || num_players > 6) {
    fprintf(stderr, "Invalid --num_players=%d (supported: 2-6)\n", num_players);
    return 1;
  }

  // Per-player parameters: players without a --player_params segment use the
  // global flags; ';'-separated segments apply in player order.
  std::vector<PlayerParams> params(static_cast<size_t>(num_players),
                                   GlobalPlayerParams());
  try {
    const std::string spec = absl::GetFlag(FLAGS_player_params);
    size_t pos = 0;
    for (int player = 0; pos <= spec.size(); ++player) {
      const size_t sep = spec.find(';', pos);
      if (sep != std::string::npos || !Trim(spec.substr(pos)).empty()) {
        if (player >= num_players) {
          throw std::runtime_error("more parameter segments than players");
        }
        params[static_cast<size_t>(player)] = ParsePlayerParams(
            spec.substr(
                pos, sep == std::string::npos ? std::string::npos : sep - pos),
            params[static_cast<size_t>(player)]);
      }
      if (sep == std::string::npos) {
        break;
      }
      pos = sep + 1;
    }
  } catch (const std::exception &e) {
    fprintf(stderr, "--player_params error: %s\n", e.what());
    return 1;
  }

  const std::string record_dir = absl::GetFlag(FLAGS_record_games);
  if (!record_dir.empty()) {
    std::error_code ec;
    std::filesystem::create_directories(record_dir, ec);
    if (ec) {
      fprintf(stderr, "Cannot create --record_games directory %s: %s\n",
              record_dir.c_str(), ec.message().c_str());
      return 1;
    }
  }

  Stats stats;
  {
    ScreenGuard screen;
    TerminalInputGuard terminal_input;

    switch (num_players) {
      case 2:
        RunWithPlayerCount<2>(gen, stats, params, record_dir);
        break;
      case 3:
        RunWithPlayerCount<3>(gen, stats, params, record_dir);
        break;
      case 4:
        RunWithPlayerCount<4>(gen, stats, params, record_dir);
        break;
      case 5:
        RunWithPlayerCount<5>(gen, stats, params, record_dir);
        break;
      case 6:
        RunWithPlayerCount<6>(gen, stats, params, record_dir);
        break;
    }
  }  // ~ScreenGuard restores the main screen.

  printf("Games: %d | wins", stats.games);
  for (int p = 0; p < num_players; ++p) {
    printf(" %sP%d%s=%d", risk_game::PlayerColor(p).c_str(), p,
           risk_game::kColorReset, stats.wins[p]);
  }
  printf(" | draws=%d\n", stats.draws);
  return 0;
}
