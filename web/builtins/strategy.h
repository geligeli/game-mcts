#ifndef GAME_MCTS_WEB_BUILTINS_STRATEGY_H
#define GAME_MCTS_WEB_BUILTINS_STRATEGY_H

// The referee's builtins as a candidate, so the play page can seat them
// (web/stage.py stages this as "builtin"). Its params choose one: builtin=
// random, mcts or mcts_smart (the default), and iterations= a search's.

#include "bots/bot_api.h"
#include "problem/risk_builtins.h"

inline candidate::policy_t MakePolicy(const candidate::Params &params) {
  return risk_builtins::Make<candidate::kNumPlayers>(
             params.get("builtin", "mcts_smart"),
             params.GetInt("iterations", 400))
      .value();
}

#endif  // GAME_MCTS_WEB_BUILTINS_STRATEGY_H
