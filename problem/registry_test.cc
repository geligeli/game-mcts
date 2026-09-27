#include <map>
#include <random>
#include <string>

#include "game_arena/referee/game_registry.h"
#include "gtest/gtest.h"

namespace tournament_broker {
namespace {

TEST(RegistryTest, OffersRisk2) {
  ASSERT_TRUE(GameRegistry().contains("risk2"));
  EXPECT_EQ(GameRegistry().at("risk2").name, "risk2");
}

TEST(RegistryTest, ParsesBuiltinSpecs) {
  const GameDescriptor &risk2 = GameRegistry().at("risk2");
  for (const char *spec : {"random", "mcts", "mcts:iterations=5"}) {
    std::string error;
    EXPECT_TRUE(risk2.make_builtin(spec, &error).has_value())
        << spec << ": " << error;
  }
  for (const char *spec : {"minimax", "mcts:iterations=0",
                           "mcts:iterations=abc", "mcts:depth=3"}) {
    std::string error;
    EXPECT_FALSE(risk2.make_builtin(spec, &error).has_value()) << spec;
    EXPECT_FALSE(error.empty()) << spec;
  }
}

// Unknown keys and bad values are ignored rather than fatal.
TEST(RegistryTest, ToleratesUnknownOrBadOptions) {
  SetRegistryOptions({{"unknown", "1"}, {"mcts_iterations", "-3"}});
  SetRegistryOptions({{"mcts_iterations", "400"}});
}

// Both builtins answer every turn with an action the referee accepts, over the
// same bytes a remote bot would see.
TEST(RegistryTest, BuiltinsPlayLegalRisk2Moves) {
  const GameDescriptor &risk2 = GameRegistry().at("risk2");
  std::string error;
  const auto random = risk2.make_builtin("random", &error);
  const auto mcts = risk2.make_builtin("mcts:iterations=5", &error);
  ASSERT_TRUE(random.has_value() && mcts.has_value()) << error;

  std::mt19937 gen(7);
  auto session = risk2.new_session();
  int decisions = 0;
  for (int step = 0; step < 400 && !session->Outcome().has_value(); ++step) {
    if (session->IsChanceNode()) {
      session->ApplyChanceAction(gen);
      continue;
    }
    const BuiltinFn &builtin = session->CurrentPlayer() == 0 ? *random : *mcts;
    ASSERT_TRUE(session->ApplySerializedAction(
        builtin(session->SerializeState(), gen), &error))
        << "step " << step << ": " << error;
    ++decisions;
  }
  EXPECT_GT(decisions, 0);
}

}  // namespace
}  // namespace tournament_broker
