#ifndef GAME_MCTS_GAME_MCTS_GAMES_RISK_STRATEGIES_EXPECTED_BATTLE_OUTCOMES_H
#define GAME_MCTS_GAME_MCTS_GAMES_RISK_STRATEGIES_EXPECTED_BATTLE_OUTCOMES_H
#include "game_mcts/games/risk/strategies/predict_battle_outcome.h"

namespace risk_game {

// Expected remaining armies when fighting until one side is wiped out. Both
// values are *marginal* expectations over all battles and can both be
// positive for close matchups; compare them to decide the winner.
BattleRemnants LookupExpectedRemnants(int attackers, int defenders);

}  // namespace risk_game

#endif  // GAME_MCTS_GAME_MCTS_GAMES_RISK_STRATEGIES_EXPECTED_BATTLE_OUTCOMES_H
