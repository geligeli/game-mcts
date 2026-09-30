#ifndef GAME_MCTS_PROBLEM_RISK_VIEW_H
#define GAME_MCTS_PROBLEM_RISK_VIEW_H

// The replay view of risk2 and risk3: the board after a step as JSON, with
// what the step touched and its dice, for problem/risk_replay.js to draw in
// the browser. A few hundred bytes, so every step gets one.
//
//   {"r":12,"m":40,"p":0,"o":"0112-...","u":[3,1,...],"rv":[7,0,0],
//    "h":[1,20],"a":[1,20],"d":[[6,5,4],[5,3]],"df":2,"c":20,"w":"..."}
//
// r: the round, and m its cap (absent when uncapped). p: the player to move,
// -1 at dice. o: each territory's owner by Country index, '-' for none.
// u: armies. rv: reserves, one per seat. h: the territories the step touched.
// a: an arrow, from and to. d: the dice, highest first, as they are compared,
// and df the defending seat. c: the territory a roll conquered. w: how the
// game ended, on its last step.
//
// Also the rest of what the tournament adds to the rules without protobuf, so
// the referee's session and web/'s in-browser engine share one implementation:
// the verdict, a place per seat (ResultOf), and every step's caption and view
// (StepRenderer, which also keeps the order seats were knocked out in).

#include <algorithm>
#include <array>
#include <cstddef>
#include <functional>
#include <numeric>
#include <string>
#include <tuple>
#include <variant>
#include <vector>

#include "game_mcts/core/mcts/game_traits.h"
#include "game_mcts/games/risk/risk_game.h"
#include "game_mcts/games/risk/risk_render.h"

namespace tournament_broker {

// What a step did, beyond the board it left.
struct StepView {
  risk_game::BoardMarks marks_;
  const risk_game::RollDiceAction *roll_ = nullptr;
  int defender_ = -1;  // the seat that rolled against |roll_|
  int conquered_ = -1;
  std::string result_;  // non-empty on the game's last step
};

template <size_t N>
std::string ViewJson(const risk_game::RiskState<N> &state, int round,
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
    json += ",\"d\":[" + rolled(attacker) + "," + rolled(defender) +
            "],\"df\":" + std::to_string(step.defender_);
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
template <size_t N>
int Rounds(const risk_game::RiskState<N> &state) {
  const auto placement_rounds =
      static_cast<int>(state.num_initial_placements_ / N);
  return state.initial_placement_
             ? 0
             : static_cast<int>(state.turn_count_) - placement_rounds;
}

struct RiskResult {
  bool over_ = false;
  // By seat, 0 first; tied seats share the better place. Empty while the
  // game goes on.
  std::vector<int> places_ = {};
  std::string text_ = {};  // how it ended; empty while it goes on
};

// Over on the whole map, or after |max_rounds| full rounds (<= 0: no cap).
// Survivors place by territories, then armies, and a seat knocked out below
// every survivor, the last of |eliminated| to go best; an exact tie is left
// level. |over| false still places the seats: how they stand now.
template <size_t N>
RiskResult ResultOf(const risk_game::RiskState<N> &state, int max_rounds,
                    const std::vector<int> &eliminated, bool over = false) {
  const mcts::game_state_t rules = state.current_state();
  const auto *win = std::get_if<mcts::win_t>(&rules);
  if (!over && win == nullptr &&
      (max_rounds <= 0 || Rounds(state) < max_rounds)) {
    return {};
  }
  std::array<int, N> territories{};
  std::array<int, N> armies{};
  for (const risk_game::Territory &t : state.map_) {
    if (t.owner >= 0) {
      ++territories[t.owner];
      armies[t.owner] += static_cast<int>(t.units);
    }
  }
  const auto rank = [&](int seat) {
    const auto out = std::ranges::find(eliminated, seat);
    return std::tuple(static_cast<int>(eliminated.end() - out),
                      -territories[seat], -armies[seat]);
  };
  std::vector<int> order(N);
  std::iota(order.begin(), order.end(), 0);
  std::ranges::stable_sort(order, {}, rank);
  static constexpr const char *kOrdinal[] = {"1st", "2nd", "3rd",
                                             "4th", "5th", "6th"};
  RiskResult result{.over_ = true, .places_ = std::vector<int>(N)};
  std::string standing;
  for (std::size_t i = 0; i < N; ++i) {
    const int seat = order[i];
    const int place = i > 0 && rank(seat) == rank(order[i - 1])
                          ? result.places_[order[i - 1]]
                          : static_cast<int>(i);
    result.places_[seat] = place;
    standing +=
        std::string(i > 0 ? ", " : "") + kOrdinal[place] + " P" +
        std::to_string(seat) +
        (std::get<0>(rank(seat)) > 0
             ? " (out)"
             : " (" + std::to_string(territories[seat]) + " territories, " +
                   std::to_string(armies[seat]) + " armies)");
  }
  result.text_ = win != nullptr ? "P" + std::to_string(win->winning_player) +
                                      " wins: the whole map"
                                : "Round cap: " + standing;
  return result;
}

// Every step's caption (what it did; a turn's last also sums the turn up) and
// view (ViewJson of the board it left), and who was knocked out, in order.
template <size_t N>
class StepRenderer {
 public:
  explicit StepRenderer(int max_rounds) : max_rounds_(max_rounds) {}

  void Render(const risk_game::RiskState<N> &before,
              const risk_game::RiskAction &action,
              const risk_game::RiskState<N> &after) {
    const auto owns = [](const risk_game::RiskState<N> &state, int seat) {
      return std::ranges::any_of(
          state.map_,
          [&](const risk_game::Territory &t) { return t.owner == seat; });
    };
    for (int seat = 0; seat < static_cast<int>(N); ++seat) {
      if (owns(before, seat) && !owns(after, seat)) {
        eliminated_.push_back(seat);
      }
    }
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
      step.defender_ = before.map_[target].owner;
      if (after.map_[target].owner != before.map_[target].owner) {
        step.conquered_ = target;
      }
    }
    step.result_ = ResultOf(after, max_rounds_, eliminated_).text_;
    view_ = ViewJson(after, Rounds(after), max_rounds_, step);
  }

  const std::string &caption() const { return caption_; }
  const std::string &view() const { return view_; }
  const std::vector<int> &eliminated() const { return eliminated_; }

 private:
  const int max_rounds_;
  std::string caption_;
  std::string view_;
  risk_game::TurnTally tally_;
  std::vector<int> eliminated_;
};

}  // namespace tournament_broker

#endif  // GAME_MCTS_PROBLEM_RISK_VIEW_H
