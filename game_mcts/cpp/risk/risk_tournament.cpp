// Runs a round-robin ELO tournament between Risk policies configured via an
// INI file. Example:
//   bazel run //game_mcts/cpp/risk:risk_tournament --
//       --config=cpp/risk/example_tournament.cfg --games_per_pair=2 --threads=4
//
// The game is fixed to 2-player Risk (RiskState<2>). More than 2 players
// would need a multiplayer rating system instead of pairwise ELO.

#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <random>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "game_mcts/cpp/risk/risk.pb.h"
#include "game_mcts/cpp/risk/risk_game.h"
#include "game_mcts/cpp/risk/risk_serialization.h"
#include "game_mcts/cpp/risk/strategies/risk_proposer.h"
#include "game_mcts/cpp/risk/strategies/risk_rollout_shortcuts.h"
#include "game_mcts/cpp/mcts/mcts.inl"
#include "game_mcts/cpp/mcts/tournament.h"
#include "google/protobuf/text_format.h"

ABSL_FLAG(std::string, config, "",
          "Path to the INI policy config file (required)");
ABSL_FLAG(int, games_per_pair, 20,
          "Games played per unordered policy pair (seats alternate)");
ABSL_FLAG(int, threads, 0, "Worker threads (0 = hardware_concurrency)");
ABSL_FLAG(int, seed, -1, "RNG seed (-1 = random)");
// Chance-node resolutions (dice rolls) count as moves too, and a 2-player
// Risk game runs ~5k-20k such steps, so the cap needs to be well above that:
// 1000 would turn every game into a draw.
ABSL_FLAG(int, max_moves_per_game, 50000,
          "Safety cap on moves per game before declaring a draw");
ABSL_FLAG(std::string, output, "",
          "Write per-game results to this CSV file (empty = disabled)");
ABSL_FLAG(double, k_factor, 16.0, "ELO K factor");
// Recording writes one protobuf-text RiskTrajectory per game, a few MB each
// (games run 5k-50k steps), so it is meant for small --games_per_pair runs.
ABSL_FLAG(std::string, record_games, "",
          "Write every game as a protobuf-text RiskTrajectory "
          "(game_<index>_<nameA>_vs_<nameB>.pbtxt) into this directory, which "
          "is created if missing. Empty = disabled. Meant for small "
          "--games_per_pair runs: each file is a few MB.");

namespace {

using game_t = risk_game::RiskState<2>;
using proposer_t = risk_game::RiskProposer<2>;
using Params = std::map<std::string, std::string>;
using mcts::tournament::AnyPolicy;

// Plays a single draw from its proposer. Parameterised on the proposer, so
// two proposal policies can be entered into the same tournament.
template <typename PROPOSER = proposer_t>
struct RandomPolicy {
  PROPOSER proposer{};

  auto operator()(const game_t &game, std::mt19937 &gen) const
      -> mcts::tournament::PolicyDecision<game_t> {
    const risk_game::RiskAction action = proposer.sample(game, gen);
    return {.action = action, .successor = game.apply_action(action)};
  }
};

// Runs the MCTS idiom (see risk_mcts_selfplay.cc PlayMove) and applies the
// best action. |PROPOSER| is what the tree expands with; the rollout policy
// carries its own.
template <typename ROLLOUT, typename PROPOSER = proposer_t>
struct RiskMctsPolicy {
  int iterations;
  double widening_c;
  double widening_alpha;
  double exploration_c;
  ROLLOUT rollout_policy;
  PROPOSER proposer{};

  auto operator()(const game_t &game, std::mt19937 &gen) const
      -> mcts::tournament::PolicyDecision<game_t> {
    mcts::MctsRunner<game_t, PROPOSER, ROLLOUT> runner(game, proposer,
                                                       rollout_policy);
    auto picker = mcts::MctsStochasticNodePicker<game_t>(
        gen, widening_c, widening_alpha, exploration_c);
    for (int i = 0; i < iterations; ++i) {
      runner.OneIteration(picker, gen);
    }
    const risk_game::RiskAction action = runner.best_action();
    return {.action = action, .successor = game.apply_action(action)};
  }
};

auto Trim(const std::string &s) -> std::string {
  const auto first = s.find_first_not_of(" \t\r\n");
  if (first == std::string::npos) {
    return "";
  }
  return s.substr(first, s.find_last_not_of(" \t\r\n") - first + 1);
}

// Parses an INI-style config: `[policy]` sections with `key = value` lines;
// `#` starts a comment. Returns one Params map per section.
auto ParseConfig(const std::string &path) -> std::vector<Params> {
  std::ifstream in(path);
  if (!in) {
    throw std::runtime_error("cannot open config file: " + path);
  }
  std::vector<Params> sections;
  std::string line;
  int line_number = 0;
  while (std::getline(in, line)) {
    ++line_number;
    line = Trim(line.substr(0, line.find('#')));
    if (line.empty()) {
      continue;
    }
    if (line.front() == '[') {
      if (line.back() != ']' ||
          Trim(line.substr(1, line.size() - 2)) != "policy") {
        throw std::runtime_error(path + ":" + std::to_string(line_number) +
                                 ": unknown section " + line);
      }
      sections.emplace_back();
      continue;
    }
    if (sections.empty()) {
      throw std::runtime_error(path + ":" + std::to_string(line_number) +
                               ": key outside of a [policy] section");
    }
    const auto eq = line.find('=');
    if (eq == std::string::npos) {
      throw std::runtime_error(path + ":" + std::to_string(line_number) +
                               ": expected 'key = value'");
    }
    sections.back()[Trim(line.substr(0, eq))] = Trim(line.substr(eq + 1));
  }
  return sections;
}

auto GetString(const Params &params, const std::string &key,
               const std::string &fallback) -> std::string {
  const auto it = params.find(key);
  return it == params.end() ? fallback : it->second;
}

auto GetInt(const Params &params, const std::string &key, int fallback) -> int {
  const auto it = params.find(key);
  if (it == params.end()) {
    return fallback;
  }
  try {
    return std::stoi(it->second);
  } catch (const std::exception &) {
    throw std::runtime_error("bad integer for '" + key + "': " + it->second);
  }
}

auto GetDouble(const Params &params, const std::string &key,
               double fallback) -> double {
  const auto it = params.find(key);
  if (it == params.end()) {
    return fallback;
  }
  try {
    return std::stod(it->second);
  } catch (const std::exception &) {
    throw std::runtime_error("bad number for '" + key + "': " + it->second);
  }
}

auto MakeRandomPolicy(const Params & /*params*/) -> AnyPolicy<game_t> {
  return AnyPolicy<game_t>(RandomPolicy<>{});
}

template <typename TREE_PROPOSER, typename ROLLOUT_PROPOSER>
auto MakeMctsPolicySplit(const Params &params) -> AnyPolicy<game_t> {
  const int iterations = GetInt(params, "iterations", 400);
  const double widening_c = GetDouble(params, "widening_c", 2.0);
  const double widening_alpha = GetDouble(params, "widening_alpha", 0.5);
  const double exploration_c = GetDouble(params, "exploration_c", 1.0);
  const std::string rollout = GetString(params, "rollout", "shortcut");
  if (rollout == "exact") {
    using exact_t = mcts::RandomRollout<game_t, ROLLOUT_PROPOSER>;
    return AnyPolicy<game_t>(RiskMctsPolicy<exact_t, TREE_PROPOSER>{
        iterations, widening_c, widening_alpha, exploration_c, exact_t{}});
  }
  if (rollout == "shortcut") {
    auto shortcut = mcts::MakeShortcutRollout<game_t, ROLLOUT_PROPOSER>(
        &risk_game::ResolveBattleWithExpectationInPlace<2>);
    return AnyPolicy<game_t>(RiskMctsPolicy<decltype(shortcut), TREE_PROPOSER>{
        iterations, widening_c, widening_alpha, exploration_c, shortcut});
  }
  throw std::runtime_error("unknown rollout '" + rollout +
                           "' (expected: shortcut | exact)");
}

// Dispatches on the rollout proposer name. atk/smart are rejected here on
// purpose: their attack-averse playouts stall instead of terminating.
template <typename TREE_PROPOSER>
auto MakeMctsPolicyForTree(const std::string &rollout_proposer,
                           const Params &params) -> AnyPolicy<game_t> {
  if (rollout_proposer == "stock") {
    return MakeMctsPolicySplit<TREE_PROPOSER, proposer_t>(params);
  }
  if (rollout_proposer == "border") {
    return MakeMctsPolicySplit<TREE_PROPOSER, risk_game::RiskProposer<2, true>>(
        params);
  }
  throw std::runtime_error("unknown rollout proposer '" + rollout_proposer +
                           "' (expected: stock | border)");
}

// Config keys: `proposer` sets both tree and rollout proposer (stock|border);
// the optional `tree_proposer` overrides the tree proposer only and may
// additionally be atk|smart — those only filter which actions the *tree*
// expands, so their attack aversion cannot stall the rollout.
auto MakeMctsPolicy(const Params &params) -> AnyPolicy<game_t> {
  const std::string proposer = GetString(params, "proposer", "stock");
  if (proposer != "stock" && proposer != "border") {
    throw std::runtime_error("unknown proposer '" + proposer +
                             "' (expected: stock | border)");
  }
  const std::string tree_proposer =
      GetString(params, "tree_proposer", proposer);
  if (tree_proposer == "stock") {
    return MakeMctsPolicyForTree<proposer_t>(proposer, params);
  }
  if (tree_proposer == "border") {
    return MakeMctsPolicyForTree<risk_game::RiskProposer<2, true>>(proposer,
                                                                   params);
  }
  if (tree_proposer == "atk") {
    return MakeMctsPolicyForTree<risk_game::RiskProposer<2, false, true>>(
        proposer, params);
  }
  if (tree_proposer == "smart") {
    return MakeMctsPolicyForTree<risk_game::RiskProposer<2, true, true>>(
        proposer, params);
  }
  throw std::runtime_error("unknown tree_proposer '" + tree_proposer +
                           "' (expected: stock | border | atk | smart)");
}

using PolicyFactory = std::function<AnyPolicy<game_t>(const Params &)>;
const std::unordered_map<std::string, PolicyFactory> kPolicyRegistry = {
    {"random", MakeRandomPolicy},
    {"mcts", MakeMctsPolicy},
};

using traits_t = mcts::GameSerializationTraits<game_t>;

// One recorded game: the trajectory proto plus the file it is written to.
struct GameRecording {
  risk_game::proto::RiskTrajectory trajectory;
  std::filesystem::path path;
};

// Keeps [A-Za-z0-9-_]; maps everything else to '_', so policy names are safe
// path components.
auto SanitizeFilename(const std::string &name) -> std::string {
  std::string out;
  out.reserve(name.size());
  for (const char c : name) {
    const auto uc = static_cast<unsigned char>(c);
    out += (std::isalnum(uc) != 0 || c == '-' || c == '_') ? c : '_';
  }
  return out.empty() ? "policy" : out;
}

}  // namespace

auto main(int argc, char **argv) -> int {
  absl::ParseCommandLine(argc, argv);

  const std::string config_path = absl::GetFlag(FLAGS_config);
  if (config_path.empty()) {
    fprintf(stderr, "Missing required --config=<path to policy config>\n");
    return 1;
  }

  std::vector<AnyPolicy<game_t>> policies;
  std::vector<std::string> names;
  try {
    for (const Params &params : ParseConfig(config_path)) {
      const std::string name = GetString(params, "name", "");
      const std::string type = GetString(params, "type", "");
      if (name.empty() || type.empty()) {
        throw std::runtime_error(
            "every [policy] section needs 'name' and 'type'");
      }
      const auto it = kPolicyRegistry.find(type);
      if (it == kPolicyRegistry.end()) {
        throw std::runtime_error("unknown policy type '" + type + "' for '" +
                                 name + "'");
      }
      policies.push_back(it->second(params));
      names.push_back(name);
    }
  } catch (const std::exception &e) {
    fprintf(stderr, "Config error: %s\n", e.what());
    return 1;
  }
  if (policies.size() < 2) {
    fprintf(stderr, "Need at least 2 [policy] sections in %s\n",
            config_path.c_str());
    return 1;
  }

  int num_threads = absl::GetFlag(FLAGS_threads);
  if (num_threads <= 0) {
    num_threads = static_cast<int>(std::thread::hardware_concurrency());
    if (num_threads <= 0) {
      num_threads = 1;
    }
  }

  const int seed_flag = absl::GetFlag(FLAGS_seed);
  const uint32_t base_seed = seed_flag >= 0 ? static_cast<uint32_t>(seed_flag)
                                            : std::random_device{}();

  const auto tasks = mcts::tournament::BuildRoundRobin(
      static_cast<int>(policies.size()), absl::GetFlag(FLAGS_games_per_pair));
  printf(
      "Tournament: %zu policies, %d games/pair, %zu games total, %d "
      "threads, seed %u\n",
      policies.size(), absl::GetFlag(FLAGS_games_per_pair), tasks.size(),
      num_threads, base_seed);

  const int max_moves = absl::GetFlag(FLAGS_max_moves_per_game);
  const std::string record_dir = absl::GetFlag(FLAGS_record_games);

  std::vector<mcts::tournament::MatchRecord> records;
  if (record_dir.empty()) {
    records = mcts::tournament::RunTournament(
        tasks, [] { return game_t{}; }, policies, num_threads, base_seed,
        max_moves);
  } else {
    std::error_code ec;
    std::filesystem::create_directories(record_dir, ec);
    if (ec) {
      fprintf(stderr, "Cannot create --record_games directory %s: %s\n",
              record_dir.c_str(), ec.message().c_str());
      return 1;
    }
    // One recording slot per task; each game's observer (created by the
    // factory on the worker thread running the task) appends only to its own
    // slot, so no locking is needed.
    std::vector<GameRecording> recordings(tasks.size());
    const auto initial_state = traits_t::StateToProto(game_t{});
    for (std::size_t i = 0; i < tasks.size(); ++i) {
      *recordings[i].trajectory.mutable_initial_state() = initial_state;
      recordings[i].path = std::filesystem::path(record_dir) /
                           ("game_" + std::to_string(i) + "_" +
                            SanitizeFilename(names[tasks[i].a]) + "_vs_" +
                            SanitizeFilename(names[tasks[i].b]) + ".pbtxt");
    }
    auto observer_factory = [&](std::size_t task_index) {
      GameRecording *recording = &recordings[task_index];
      return [recording](const game_t & /*state_before*/, int player,
                         const risk_game::RiskAction &action) {
        auto *step = recording->trajectory.add_steps();
        step->set_player(player);
        *step->mutable_action() = traits_t::ActionToProto(action);
      };
    };
    records = mcts::tournament::RunTournament(
        tasks, [] { return game_t{}; }, policies, num_threads, base_seed,
        max_moves, observer_factory);

    // Fill in the outcome and write one .pbtxt per game.
    for (std::size_t i = 0; i < recordings.size(); ++i) {
      auto &trajectory = recordings[i].trajectory;
      if (records[i].score_a == 0.5) {
        trajectory.set_result(risk_game::proto::RiskTrajectory::DRAW);
      } else {
        trajectory.set_result(risk_game::proto::RiskTrajectory::WIN);
        trajectory.set_winning_player(records[i].score_a == 1.0 ? 0 : 1);
      }
      std::string text;
      if (!google::protobuf::TextFormat::PrintToString(trajectory, &text)) {
        fprintf(stderr, "TextFormat failed for game %zu\n", i);
        return 1;
      }
      std::ofstream out(recordings[i].path);
      if (!out) {
        fprintf(stderr, "Cannot open %s\n",
                recordings[i].path.string().c_str());
        return 1;
      }
      out << text;
    }
    printf("Wrote %zu game trajectories to %s\n", recordings.size(),
           record_dir.c_str());
  }

  const auto ratings =
      mcts::tournament::ComputeElo(records, static_cast<int>(policies.size()),
                                   absl::GetFlag(FLAGS_k_factor));
  mcts::tournament::PrintStandings(ratings, names, records);

  const std::string output_path = absl::GetFlag(FLAGS_output);
  if (!output_path.empty()) {
    std::ofstream out(output_path);
    if (!out) {
      fprintf(stderr, "Cannot open --output file: %s\n", output_path.c_str());
      return 1;
    }
    out << "policy_a,policy_b,name_a,name_b,score_a\n";
    for (const auto &record : records) {
      out << record.policy_a << ',' << record.policy_b << ','
          << names[record.policy_a] << ',' << names[record.policy_b] << ','
          << record.score_a << '\n';
    }
  }
  return 0;
}
