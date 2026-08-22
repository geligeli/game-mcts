#include "game_mcts/cpp/risk/strategies/predict_battle_outcome.h"

#include <algorithm>
#include <vector>

namespace risk_game {

namespace {
struct BattleOutcome {
  double a2_d0 = 0.0;  // Attacker wins 2
  double a1_d1 = 0.0;  // Both lose 1
  double a0_d2 = 0.0;  // Defender wins 2
  double a1_d0 = 0.0;  // Attacker wins 1
  double a0_d1 = 0.0;  // Defender wins 1
};

BattleOutcome get_probs(int a, int d) {
  int dice_a = std::min(a, 3);
  int dice_d = std::min(d, 2);

  if (dice_a == 3 && dice_d == 2)
    return {2890.0 / 7776.0, 2611.0 / 7776.0, 2275.0 / 7776.0, 0, 0};
  if (dice_a == 2 && dice_d == 2)
    return {295.0 / 1296.0, 420.0 / 1296.0, 581.0 / 1296.0, 0, 0};
  if (dice_a == 1 && dice_d == 2) return {0, 0, 0, 55.0 / 216.0, 161.0 / 216.0};
  if (dice_a == 3 && dice_d == 1)
    return {0, 0, 0, 855.0 / 1296.0, 441.0 / 1296.0};
  if (dice_a == 2 && dice_d == 1) return {0, 0, 0, 125.0 / 216.0, 91.0 / 216.0};
  if (dice_a == 1 && dice_d == 1) return {0, 0, 0, 15.0 / 36.0, 21.0 / 36.0};
  return {};
}

}  // namespace

BattleRemnants ComputeExpectedRemnants(int initial_a, int initial_d) {
  // dp[a][d] stores the probability of the battle reaching the state (a, d).
  // Transitions only ever move to rows a, a-1 and a-2, so a rolling window
  // of three rows suffices: row (a % 3) of the buffer holds row a, and each
  // row's slot is cleared once the row is fully consumed. The floating-point
  // operations are exactly those of the previous full-table version, so the
  // results are bit-identical while large battles no longer allocate (or
  // page through) O(a*d) memory per call.
  const int cols = initial_d + 1;
  std::vector<double> dp_storage(3 * static_cast<size_t>(cols), 0.0);
  const auto dp = [cols, &dp_storage](int a, int d) -> double& {
    return dp_storage[static_cast<size_t>(a % 3) * cols + d];
  };

  dp(initial_a, initial_d) = 1.0;

  double exp_attackers = 0.0;
  double exp_defenders = 0.0;

  for (int a = initial_a; a >= 0; --a) {
    for (int d = initial_d; d >= 0; --d) {
      if (dp(a, d) <= 0) continue;

      // If a side is wiped out, add to expectation and don't process further
      // transitions
      if (a == 0) {
        exp_defenders += d * dp(a, d);
        continue;
      }
      if (d == 0) {
        exp_attackers += a * dp(a, d);
        continue;
      }

      BattleOutcome p = get_probs(a, d);

      if (p.a2_d0 > 0) {
        if (d >= 2) {
          dp(a, d - 2) += dp(a, d) * p.a2_d0;
        }
        dp(a - 1, d - 1) += dp(a, d) * p.a1_d1;
        if (a >= 2) {
          dp(a - 2, d) += dp(a, d) * p.a0_d2;
        }
      } else {
        dp(a, d - 1) += dp(a, d) * p.a1_d0;
        dp(a - 1, d) += dp(a, d) * p.a0_d1;
      }
    }
    // Row a is fully consumed; clear its slot for reuse as row a-3.
    std::fill_n(dp_storage.begin() + static_cast<size_t>(a % 3) * cols, cols,
                0.0);
  }

  return {static_cast<int>(exp_attackers + 0.5),
          static_cast<int>(exp_defenders + 0.5)};
}

}  // namespace risk_game