#ifndef RISK_GAME_AI_CPP_RISK_STRATEGIES_PREDICT_BATTLE_OUTCOME_H
#define RISK_GAME_AI_CPP_RISK_STRATEGIES_PREDICT_BATTLE_OUTCOME_H
namespace risk_game {

struct BattleRemnants {
  int attackers;
  int defenders;
};

BattleRemnants ComputeExpectedRemnants(int initial_a, int initial_d);

}  // namespace risk_game

#endif  // RISK_GAME_AI_CPP_RISK_STRATEGIES_PREDICT_BATTLE_OUTCOME_H
