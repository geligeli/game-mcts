// Every problem config in problems/ must load.
//
// The arena runs this over its own reference problem; this is the same check
// for the problems whose solutions are game_mcts code. They are the deployment
// surface: a typo in one is otherwise only discovered when a server refuses to
// start, which is the worst moment to find out.

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

#include "game_arena/server/problem_config.h"

namespace tournament_arena {
namespace {

// Runfiles-relative; a cc_test runs with the runfiles root as its cwd.
constexpr char kProblemsDir[] = "game_mcts/arena/problems";

TEST(GameMctsProblemsTest, EveryConfigLoads) {
  ASSERT_TRUE(std::filesystem::is_directory(kProblemsDir))
      << "problems/ not in runfiles; check the test's data dependency";

  std::vector<std::string> loaded;
  for (const auto &entry : std::filesystem::directory_iterator(kProblemsDir)) {
    if (entry.path().extension() != ".textproto") {
      continue;
    }
    std::string error;
    const auto config = LoadProblemConfig(entry.path(), &error);
    EXPECT_TRUE(config.has_value()) << error;
    if (config) {
      loaded.push_back(config->problem_id());
    }
  }

  // A glob that silently matches nothing would make this test pass forever.
  EXPECT_GE(loaded.size(), 2u)
      << "expected the shipped match and grade problems";
  EXPECT_NE(std::find(loaded.begin(), loaded.end(), "risk2"), loaded.end());
  EXPECT_NE(std::find(loaded.begin(), loaded.end(), "mcts-bench"),
            loaded.end());
}

// The structured submission path only exists if the problem says what to
// compile a solution against. risk2 offers it, so it must stay complete.
TEST(GameMctsProblemsTest, Risk2DeclaresItsCandidateHarness) {
  std::string error;
  const auto config = LoadProblemConfig(
      std::filesystem::path(kProblemsDir) / "risk2.textproto", &error);
  ASSERT_TRUE(config.has_value()) << error;
  const proto::CandidateHarness &harness = config->submission().harness();
  EXPECT_FALSE(config->submission().files_submit_dir().empty());
  EXPECT_FALSE(harness.api_dep().empty());
  EXPECT_FALSE(harness.main_src().empty());
  EXPECT_FALSE(harness.game_define().empty());
}

}  // namespace
}  // namespace tournament_arena
