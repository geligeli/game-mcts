#ifndef GAME_MCTS_PROBLEM_RISK_VIEW_H
#define GAME_MCTS_PROBLEM_RISK_VIEW_H

// risk2's replay view: the board after a step as JSON, with what the step
// touched and its dice, for problem/risk2_replay.js to draw in the browser.
// A few hundred bytes, so every step gets one.
//
//   {"r":12,"m":40,"p":0,"o":"0110-...","u":[3,1,...],"rv":[7,0],
//    "h":[1,20],"a":[1,20],"d":[[6,5,4],[5,3]],"c":20,"w":"..."}
//
// r: the round, and m its cap (absent when uncapped). p: the player to move,
// -1 at dice. o: each territory's owner by Country index, '-' for none.
// u: armies. rv: reserves. h: the territories the step touched. a: an arrow,
// from and to. d: the dice, highest first, as they are compared. c: the
// territory a roll conquered. w: how the game ended, on its last step.

#include <algorithm>
#include <functional>
#include <string>

#include "game_mcts/games/risk/risk_game.h"
#include "game_mcts/games/risk/risk_render.h"

namespace tournament_broker {

// What a step did, beyond the board it left.
struct StepView {
  risk_game::BoardMarks marks_;
  const risk_game::RollDiceAction *roll_ = nullptr;
  int conquered_ = -1;
  std::string result_;  // non-empty on the game's last step
};

inline std::string ViewJson(const risk_game::RiskState<2> &state, int round,
                            int max_rounds, const StepView &step) {
  const auto ints = [](auto &&values) {
    std::string out = "[";
    for (const int value : values) {
      out += (out.size() > 1 ? "," : "") + std::to_string(value);
    }
    return out + "]";
  };
  std::string owners;
  std::string units = "[";
  for (const risk_game::Territory &t : state.map_) {
    owners += t.owner < 0 ? '-' : static_cast<char>('0' + t.owner);
    units += (units.size() > 1 ? "," : "") + std::to_string(t.units);
  }
  std::string json = "{\"r\":" + std::to_string(round);
  if (max_rounds > 0) {
    json += ",\"m\":" + std::to_string(max_rounds);
  }
  json += ",\"p\":" + std::to_string(state.current_player_) + ",\"o\":\"" +
          owners + "\",\"u\":" + units + "],\"rv\":" + ints(state.reserves_);
  std::string touched = "[";
  for (int t = 0; t < risk_game::kNumTerritories; ++t) {
    if (step.marks_.highlighted_[t]) {
      touched += (touched.size() > 1 ? "," : "") + std::to_string(t);
    }
  }
  if (touched.size() > 1) {
    json += ",\"h\":" + touched + "]";
  }
  if (step.marks_.arrow_from_ >= 0) {
    json += ",\"a\":[" + std::to_string(step.marks_.arrow_from_) + "," +
            std::to_string(step.marks_.arrow_to_) + "]";
  }
  if (step.roll_ != nullptr) {
    auto attacker = step.roll_->attacker_rolls_;
    auto defender = step.roll_->defender_rolls_;
    std::ranges::sort(attacker, std::greater<>());
    std::ranges::sort(defender, std::greater<>());
    const auto rolled = [](const auto &faces) {
      std::string out = "[";
      for (const int face : faces) {
        if (face > 0) {
          out += (out.size() > 1 ? "," : "") + std::to_string(face);
        }
      }
      return out + "]";
    };
    json += ",\"d\":[" + rolled(attacker) + "," + rolled(defender) + "]";
  }
  if (step.conquered_ >= 0) {
    json += ",\"c\":" + std::to_string(step.conquered_);
  }
  if (!step.result_.empty()) {
    json += ",\"w\":\"";
    for (const char c : step.result_) {
      if (c == '"' || c == '\\') {
        json += '\\';
      }
      json += c;
    }
    json += "\"";
  }
  return json + "}";
}

}  // namespace tournament_broker

#endif  // GAME_MCTS_PROBLEM_RISK_VIEW_H
