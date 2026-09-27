#ifndef GAME_MCTS_PROBLEM_RISK_SESSION_H
#define GAME_MCTS_PROBLEM_RISK_SESSION_H

// risk2: two-player Risk, plus what a tournament needs on top of the rules.
//
// - A round cap. Risk only ends when one side owns the whole map, so passive
//   play never ends, and the referee's move cap settles a draw by thinking
//   time, which rewards the fastest staller. After max_rounds full rounds the
//   side holding more territories wins, then the one with more armies; only
//   an exact tie is left to the referee.
// - A replay. Every step gets a caption (RenderLastStep()) and a view
//   (RenderState()): the board it left as a JSON snapshot (problem/risk_view.h)
//   with what it touched and its dice, which problem/risk2_replay.js draws on
//   the map in the browser.

#include <array>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

#include "game_arena/referee/game_session.h"
#include "game_mcts/core/mcts/serialization.h"
#include "game_mcts/games/risk/risk_game.h"
#include "game_mcts/games/risk/risk_render.h"
#include "game_mcts/games/risk/risk_serialization.h"
#include "problem/risk_view.h"
#include "problem/session.h"

namespace tournament_broker {

class RiskSession final : public GameSessionImpl<risk_game::RiskState<2>> {
 public:
  using state_t = risk_game::RiskState<2>;

  // max_rounds <= 0: no cap.
  explicit RiskSession(int max_rounds, state_t initial = state_t{})
      : GameSessionImpl(std::move(initial)), max_rounds_(max_rounds) {
    initial_view_ = ViewJson(State(), Rounds(State()), max_rounds_, {});
  }

  // Full rounds (every player one turn) since initial placement ended.
  static int Rounds(const state_t &state) {
    const auto placement_rounds =
        static_cast<int>(state.num_initial_placements_ / 2);
    return state.initial_placement_
               ? 0
               : static_cast<int>(state.turn_count_) - placement_rounds;
  }

  std::optional<GameOutcome> Outcome() const override {
    if (auto outcome = GameSessionImpl::Outcome()) {
      return outcome;
    }
    const state_t &state = State();
    if (max_rounds_ <= 0 || Rounds(state) < max_rounds_) {
      return std::nullopt;
    }
    const Score score = Holdings(state);
    const int winner = Ahead(score.territories_) >= 0
                           ? Ahead(score.territories_)
                           : Ahead(score.armies_);
    if (winner < 0) {
      return GameOutcome{.is_draw = true};
    }
    return GameOutcome{.is_draw = false, .winning_player = winner};
  }

  bool ApplySerializedAction(std::string_view bytes,
                             std::string *error) override {
    const state_t before = State();
    if (!GameSessionImpl::ApplySerializedAction(bytes, error)) {
      return false;
    }
    OnStep(before);
    return true;
  }

  void ApplyChanceAction(std::mt19937 &gen) override {
    const state_t before = State();
    GameSessionImpl::ApplyChanceAction(gen);
    OnStep(before);
  }

  std::string RenderState() const override {
    return Steps().empty() ? initial_view_ : view_;
  }

  std::string RenderLastStep() const override { return caption_; }

 private:
  struct Score {
    std::array<int, 2> territories_{};
    std::array<int, 2> armies_{};
  };

  static Score Holdings(const state_t &state) {
    Score score;
    for (const risk_game::Territory &t : state.map_) {
      if (t.owner == 0 || t.owner == 1) {
        ++score.territories_[t.owner];
        score.armies_[t.owner] += static_cast<int>(t.units);
      }
    }
    return score;
  }

  // The seat ahead on |score|, or -1 when level.
  static int Ahead(const std::array<int, 2> &score) {
    return score[0] == score[1] ? -1 : score[0] > score[1] ? 0 : 1;
  }

  std::string ResultText(const GameOutcome &outcome) const {
    if (GameSessionImpl::Outcome().has_value()) {
      return "P" + std::to_string(outcome.winning_player) +
             " wins: the whole map";
    }
    if (outcome.is_draw) {
      return "Round cap: a draw, territories and armies level";
    }
    const Score score = Holdings(State());
    const bool on_territories = Ahead(score.territories_) >= 0;
    const auto &by = on_territories ? score.territories_ : score.armies_;
    return "Round cap: P" + std::to_string(outcome.winning_player) +
           " wins on " + (on_territories ? "territories " : "armies ") +
           std::to_string(by[0]) + ":" + std::to_string(by[1]);
  }

  void OnStep(const state_t &before) {
    const state_t &after = State();
    traits::action_proto_t proto;
    proto.ParseFromString(Steps().back().action_bytes);
    const risk_game::RiskAction action = traits::ActionFromProto(proto);

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
    if (const auto outcome = Outcome()) {
      step.result_ = ResultText(*outcome);
    }
    view_ = ViewJson(after, Rounds(after), max_rounds_, step);
  }

  const int max_rounds_;
  std::string initial_view_;
  std::string caption_;
  std::string view_;
  risk_game::TurnTally tally_;
};

}  // namespace tournament_broker

#endif  // GAME_MCTS_PROBLEM_RISK_SESSION_H
