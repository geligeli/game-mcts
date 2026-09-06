#ifndef GAME_MCTS_GAME_MCTS_GAMES_RISK_STRATEGIES_PREDICT_BATTLE_OUTCOME_H
#define GAME_MCTS_GAME_MCTS_GAMES_RISK_STRATEGIES_PREDICT_BATTLE_OUTCOME_H
namespace risk_game {

struct BattleRemnants {
  int attackers;
  int defenders;
};

BattleRemnants ComputeExpectedRemnants(int initial_a, int initial_d);

}  // namespace risk_game

#endif  // GAME_MCTS_GAME_MCTS_GAMES_RISK_STRATEGIES_PREDICT_BATTLE_OUTCOME_H
