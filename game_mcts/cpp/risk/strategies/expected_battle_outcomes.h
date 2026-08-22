#ifndef RISK_GAME_AI_CPP_RISK_STRATEGIES_EXPECTED_BATTLE_OUTCOMES_H
#define RISK_GAME_AI_CPP_RISK_STRATEGIES_EXPECTED_BATTLE_OUTCOMES_H
#include "cpp/risk/strategies/predict_battle_outcome.h"

namespace risk_game {

// Expected remaining armies when fighting until one side is wiped out. Both
// values are *marginal* expectations over all battles and can both be
// positive for close matchups; compare them to decide the winner.
BattleRemnants LookupExpectedRemnants(int attackers, int defenders);

}  // namespace risk_game

#endif  // RISK_GAME_AI_CPP_RISK_STRATEGIES_EXPECTED_BATTLE_OUTCOMES_H
