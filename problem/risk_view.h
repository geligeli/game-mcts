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
//
// Also the rest of what risk2 adds to the rules without protobuf, so the
// referee's session and web/'s in-browser engine share one implementation:
// the round cap's verdict (ResultOf) and every step's caption and view
// (StepRenderer).

#include <algorithm>
#include <array>
#include <functional>
#include <string>
#include <variant>

#include "game_mcts/core/mcts/game_traits.h"
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

// Full rounds (every player one turn) since initial placement ended.
inline int Rounds(const risk_game::RiskState<2> &state) {
  const auto placement_rounds =
      static_cast<int>(state.num_initial_placements_ / 2);
  return state.initial_placement_
             ? 0
             : static_cast<int>(state.turn_count_) - placement_rounds;
}

struct RiskResult {
  bool over_ = false;
  bool draw_ = false;
  int winner_ = -1;   // meaningful iff over_ && !draw_
  std::string text_;  // how it ended; empty while it goes on
};

// The whole map, or after |max_rounds| full rounds (<= 0: no cap) the side
// holding more territories, then more armies; an exact tie is a draw.
inline RiskResult ResultOf(const risk_game::RiskState<2> &state,
                           int max_rounds) {
  const mcts::game_state_t rules = state.current_state();
  if (const auto *win = std::get_if<mcts::win_t>(&rules)) {
    return {.over_ = true,
            .winner_ = win->winning_player,
            .text_ = "P" + std::to_string(win->winning_player) +
                     " wins: the whole map"};
  }
  if (max_rounds <= 0 || Rounds(state) < max_rounds) {
    return {};
  }
  std::array<int, 2> territories{};
  std::array<int, 2> armies{};
  for (const risk_game::Territory &t : state.map_) {
    if (t.owner == 0 || t.owner == 1) {
      ++territories[t.owner];
      armies[t.owner] += static_cast<int>(t.units);
    }
  }
  // The seat ahead on |score|, or -1 when level.
  const auto ahead = [](const std::array<int, 2> &score) {
    return score[0] == score[1] ? -1 : score[0] > score[1] ? 0 : 1;
  };
  const bool on_territories = ahead(territories) >= 0;
  const auto &by = on_territories ? territories : armies;
  if (ahead(by) < 0) {
    return {.over_ = true,
            .draw_ = true,
            .text_ = "Round cap: a draw, territories and armies level"};
  }
  return {.over_ = true,
          .winner_ = ahead(by),
          .text_ = "Round cap: P" + std::to_string(ahead(by)) + " wins on " +
                   (on_territories ? "territories " : "armies ") +
                   std::to_string(by[0]) + ":" + std::to_string(by[1])};
}

// Every step's caption (what it did; a turn's last also sums the turn up) and
// view (ViewJson of the board it left).
class StepRenderer {
 public:
  explicit StepRenderer(int max_rounds) : max_rounds_(max_rounds) {}

  void Render(const risk_game::RiskState<2> &before,
              const risk_game::RiskAction &action,
              const risk_game::RiskState<2> &after) {
    caption_ = risk_game::DescribeStep(before, action, after);
    tally_.Add(before, action, after);
    if (std::holds_alternative<risk_game::FortifyAction>(action)) {
      caption_ += "  " + tally_.Describe(before.current_player_, true);
      tally_ = {};  // the turn is over
    }
    StepView step;
    step.marks_ = risk_game::StepMarks(before, action);
    step.roll_ = std::get_if<risk_game::RollDiceAction>(&action);
    if (step.roll_ != nullptr && before.queued_attack_.has_value()) {
      const int target = before.queued_attack_->target;
      if (after.map_[target].owner != before.map_[target].owner) {
        step.conquered_ = target;
      }
    }
    step.result_ = ResultOf(after, max_rounds_).text_;
    view_ = ViewJson(after, Rounds(after), max_rounds_, step);
  }

  const std::string &caption() const { return caption_; }
  const std::string &view() const { return view_; }

 private:
  const int max_rounds_;
  std::string caption_;
  std::string view_;
  risk_game::TurnTally tally_;
};

}  // namespace tournament_broker

#endif  // GAME_MCTS_PROBLEM_RISK_VIEW_H
