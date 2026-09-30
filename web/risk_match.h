#ifndef GAME_MCTS_WEB_RISK_MATCH_H
#define GAME_MCTS_WEB_RISK_MATCH_H

// One game of the season's Risk (bots/bot_api.h's candidate::game_t) between a
// person and a policy in every other seat, for the play page: the engine
// web/bot_wasm.cc exports to the browser, where it runs as WebAssembly beside
// a candidate's own policy.
//
// Every call answers JSON:
//
//   {"events":[{"k":"roll","p":1,"c":"...","v":{...}},...],"state":{...}}
//
// events: the steps the call played, in order; k their kind (place,
// reinforce, attack, defend, roll, fortify), p who took them (-1 dice), c the
// caption and v the view after them (problem/risk_view.h, as a replay has).
// state: where the game now stands, for the page to offer the next move:
//
//   {"view":{...},"phase":"attack","seat":0,"reserves":5,"result":"",
//    "places":[],"error":"..."}
//
// phase is the person's: place (initial placement), reinforce (reserves to
// place before the first attack), attack (which a fortify ends), bot (the
// policy's move: call BotStep) or over. places, once it is over, is each
// seat's, 0 first (problem/risk_view.h's ResultOf). error explains a refused
// move, which leaves the game as it was.
//
// A person never picks dice: they attack and defend with the most allowed,
// which also makes a whole battle a matter of clicks. What moves in on a
// conquest is theirs to say (QueueAttackAction::num_move_on_conquest_).

#include <array>
#include <cstdint>
#include <random>
#include <string>
#include <vector>

#include "bots/bot_api.h"
#include "game_mcts/games/risk/risk_game.h"
#include "problem/risk_view.h"

namespace risk_web {

// The season's: a candidate compiled for the page plays the game it was
// written for.
using state_t = candidate::game_t;
using policy_t = candidate::policy_t;
inline constexpr size_t kPlayers = candidate::kNumPlayers;
// Armies per territory, placed before the turn's first attack or fortify.
using placement_t = std::array<uint16_t, risk_game::kNumTerritories>;

class RiskMatch {
 public:
  // |bots| play the other seats, in seat order; |seed| drives the dice and
  // the policies; |max_rounds| <= 0: no cap.
  RiskMatch(std::vector<policy_t> bots, int human_seat, int max_rounds,
            uint32_t seed);

  // Nothing played: just where the game stands.
  std::string State() const;

  // Claims (or, once all are owned, reinforces) |territory| in initial
  // placement.
  std::string Place(int territory);
  // Deals the rest of initial placement at random, for every side, so a game
  // can start at once.
  std::string QuickSetup();
  // Places |reinforce| (all reserves, when any are left) and rolls once from
  // |source| at |target|; with |blitz|, keeps rolling until it is conquered or
  // |source| is down to one army. A conquest moves in |move| armies, clamped
  // by the rules: 0 the dice, QueueAttackAction::kMoveAll all but one.
  std::string Attack(int source, int target, const placement_t &reinforce,
                     bool blitz, int move = 0);
  // Ends the turn, after placing |reinforce| if reserves are left. |units| <= 1
  // moves nothing.
  std::string Fortify(int source, int target, int units,
                      const placement_t &reinforce);
  // The next policy's decision, and whatever follows it that needs no one
  // (dice, the person's defence), up to the next decision.
  std::string BotStep();

  // The policies decide their own defence dice by default; with |fast| they
  // roll the most allowed, which is almost always right and costs no thinking.
  void set_fast_defense(bool fast) { fast_defense_ = fast; }

  const state_t &state() const { return state_; }

 private:
  struct Event {
    std::string kind;
    int player;
    std::string caption;
    std::string view;
  };

  // Applies |action| for the player to move, if legal, and records it.
  bool Apply(const risk_game::RiskAction &action, std::string *error);
  // Plays every node that needs no one: dice, and the person's defence.
  void Settle();
  // The mover's policy's action for the node, or a random legal one if it
  // offers an illegal move.
  risk_game::RiskAction BotAction();
  // Places |reinforce| (all the reserves) on its own, unless there is nothing
  // left to place this turn.
  bool PlaceReserves(const placement_t &reinforce, std::string *error);
  std::string Answer(std::string error = "");

  std::vector<policy_t> bots_;  // the other seats', in seat order
  const int human_;
  const int max_rounds_;
  std::mt19937 gen_;
  bool fast_defense_ = false;
  state_t state_;
  tournament_broker::StepRenderer<kPlayers> renderer_;
  std::string view_;
  std::vector<Event> events_;  // since the last answer
};

// The board, for the page's own adjacency and names:
//   {"names":[...],"neighbors":[[...],...],"continents":[{"name":..,
//    "bonus":..,"members":[...]},...]}
std::string BoardJson();

}  // namespace risk_web

#endif  // GAME_MCTS_WEB_RISK_MATCH_H
