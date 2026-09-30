#include <cstddef>
#include <map>
#include <random>
#include <string>
#include <utility>

#include "game_arena/referee/game_registry.h"
#include "gtest/gtest.h"

namespace tournament_broker {
namespace {

TEST(RegistryTest, OffersRiskForTwoAndThree) {
  for (const auto &[name, seats] : {std::pair{"risk2", 2}, {"risk3", 3}}) {
    ASSERT_TRUE(GameRegistry().contains(name));
    const GameDescriptor &risk = GameRegistry().at(name);
    EXPECT_EQ(risk.name, name);
    EXPECT_EQ(risk.num_players, seats);
    std::string error;
    EXPECT_TRUE(risk.make_builtin(risk.forfeit_builtin, &error).has_value())
        << error;
  }
}

TEST(RegistryTest, ParsesBuiltinSpecs) {
  const GameDescriptor &risk2 = GameRegistry().at("risk2");
  for (const char *spec : {"random", "mcts", "mcts:iterations=5", "mcts_smart",
                           "mcts_smart:iterations=5"}) {
    std::string error;
    EXPECT_TRUE(risk2.make_builtin(spec, &error).has_value())
        << spec << ": " << error;
  }
  for (const char *spec :
       {"minimax", "mcts:iterations=0", "mcts:iterations=abc", "mcts:depth=3",
        "mcts_smart:iterations=", "mctsx"}) {
    std::string error;
    EXPECT_FALSE(risk2.make_builtin(spec, &error).has_value()) << spec;
    EXPECT_FALSE(error.empty()) << spec;
  }
}

// Unknown keys and bad values are ignored rather than fatal.
TEST(RegistryTest, ToleratesUnknownOrBadOptions) {
  SetRegistryOptions(
      {{"unknown", "1"}, {"mcts_iterations", "-3"}, {"max_rounds", "x"}});
  SetRegistryOptions({{"mcts_iterations", "400"}, {"max_rounds", "0"}});
}

// max_rounds reaches the sessions the registry makes: a capped game over a
// split board is scored instead of running on.
TEST(RegistryTest, MaxRoundsCapsNewSessions) {
  SetRegistryOptions({{"max_rounds", "3"}});
  for (const char *game : {"risk2", "risk3"}) {
    auto session = GameRegistry().at(game).new_session();
    std::mt19937 gen(3);
    std::string error;
    const auto random = GameRegistry().at(game).make_builtin("random", &error);
    ASSERT_TRUE(random.has_value()) << error;
    while (!session->Outcome().has_value()) {
      if (session->IsChanceNode()) {
        session->ApplyChanceAction(gen);
      } else {
        ASSERT_TRUE(session->ApplySerializedAction(
            (*random)(session->SerializeState(), gen), &error))
            << error;
      }
    }
    // Random play cannot take a whole board in three rounds.
    EXPECT_LT(session->MoveCount(), 3000) << game;
    EXPECT_EQ(session->Outcome()->places.size(),
              static_cast<std::size_t>(GameRegistry().at(game).num_players));
  }
  SetRegistryOptions({{"max_rounds", "0"}});
}

// Both builtins answer every turn with an action the referee accepts, over the
// same bytes a remote bot would see.
TEST(RegistryTest, BuiltinsPlayLegalMoves) {
  for (const char *game : {"risk2", "risk3"}) {
    const GameDescriptor &risk = GameRegistry().at(game);
    std::string error;
    const auto random = risk.make_builtin("random", &error);
    const auto mcts = risk.make_builtin("mcts_smart:iterations=5", &error);
    ASSERT_TRUE(random.has_value() && mcts.has_value()) << error;

    std::mt19937 gen(7);
    auto session = risk.new_session();
    int decisions = 0;
    for (int step = 0; step < 400 && !session->Outcome().has_value(); ++step) {
      if (session->IsChanceNode()) {
        session->ApplyChanceAction(gen);
        continue;
      }
      const BuiltinFn &builtin =
          session->CurrentPlayer() == 0 ? *random : *mcts;
      ASSERT_TRUE(session->ApplySerializedAction(
          builtin(session->SerializeState(), gen), &error))
          << game << " step " << step << ": " << error;
      ++decisions;
    }
    EXPECT_GT(decisions, 0) << game;
  }
}

}  // namespace
}  // namespace tournament_broker
