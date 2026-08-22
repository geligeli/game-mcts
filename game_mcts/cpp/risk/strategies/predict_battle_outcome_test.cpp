#include "cpp/risk/strategies/predict_battle_outcome.h"

#include <gtest/gtest.h>

namespace risk_game {

TEST(PredictBattleOutcome, TestExpectedRemnants) {
  int A = 10;  // Available attackers (Total units - 1)
  int D = 5;   // Total defenders

  for (int a = 1; a <= A; ++a) {
    for (int d = 1; d <= D; ++d) {
      BattleRemnants remnants = ComputeExpectedRemnants(a, d);
      EXPECT_GE(remnants.attackers, 0.0);
      EXPECT_GE(remnants.defenders, 0.0);
      EXPECT_LE(remnants.attackers, a);
      EXPECT_LE(remnants.defenders, d);
    }
  }
}

}  // namespace risk_game