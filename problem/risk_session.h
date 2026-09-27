#ifndef GAME_MCTS_PROBLEM_RISK_SESSION_H
#define GAME_MCTS_PROBLEM_RISK_SESSION_H

// risk2: two-player Risk, plus what a tournament needs on top of the rules.
//
// - A round cap. Risk only ends when one side owns the whole map, so passive
//   play never ends, and the referee's move cap settles a draw by thinking
//   time, which rewards the fastest staller. After max_rounds full rounds the
//   side holding more territories wins, then the one with more armies; only
//   an exact tie is left to the referee.
// - A replay. Every step gets a caption (RenderLastStep()). The board is drawn
//   in full once a turn, when the game's setup phases end and when it is over;
//   a conquest, a new attack and a fortify get the rows around them. Every
//   other step leaves the view as it was, which the arena's replay keeps on
//   screen.

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
#include "problem/session.h"

namespace tournament_broker {

class RiskSession final : public GameSessionImpl<risk_game::RiskState<2>> {
 public:
  using state_t = risk_game::RiskState<2>;

  // max_rounds <= 0: no cap.
  explicit RiskSession(int max_rounds, state_t initial = state_t{})
      : GameSessionImpl(std::move(initial)), max_rounds_(max_rounds) {
    initial_view_ =
        Header(State()) +
        risk_game::RenderBoard(State(), {}, risk_game::BoardDetail::kFull);
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

  std::string Header(const state_t &state) const {
    const Score score = Holdings(state);
    std::string header = "Round " + std::to_string(Rounds(state));
    if (max_rounds_ > 0) {
      header += "/" + std::to_string(max_rounds_);
    }
    return header + "  territories " + std::to_string(score.territories_[0]) +
           ":" + std::to_string(score.territories_[1]) + "  armies " +
           std::to_string(score.armies_[0]) + ":" +
           std::to_string(score.armies_[1]) + "\n";
  }

  std::string ResultLine(const GameOutcome &outcome) const {
    if (GameSessionImpl::Outcome().has_value()) {
      return "P" + std::to_string(outcome.winning_player) +
             " wins: the whole map\n";
    }
    if (outcome.is_draw) {
      return "Round cap: a draw, territories and armies level\n";
    }
    const Score score = Holdings(State());
    const bool on_territories = Ahead(score.territories_) >= 0;
    const auto &by = on_territories ? score.territories_ : score.armies_;
    return "Round cap: P" + std::to_string(outcome.winning_player) +
           " wins on " + (on_territories ? "territories " : "armies ") +
           std::to_string(by[0]) + ":" + std::to_string(by[1]) + "\n";
  }

  // The step taken from |before| was a player's first of their turn.
  static bool TurnStart(const state_t &before) {
    return !before.initial_placement_ && before.current_player_ >= 0 &&
           !before.queued_attack_.has_value() && before.first_attack_of_turn_ &&
           before.reserves_[before.current_player_] > 0;
  }

  void OnStep(const state_t &before) {
    using risk_game::BoardDetail;
    const state_t &after = State();
    traits::action_proto_t proto;
    proto.ParseFromString(Steps().back().action_bytes);
    const risk_game::RiskAction action = traits::ActionFromProto(proto);

    caption_ = risk_game::DescribeStep(before, action, after);
    tally_.Add(before, action, after);
    const auto *fortify = std::get_if<risk_game::FortifyAction>(&action);
    const auto *move = std::get_if<risk_game::PlayerAction>(&action);
    if (fortify != nullptr) {  // the turn is over
      caption_ += "  " + tally_.Describe(before.current_player_, true);
      tally_ = {};
      last_attack_.reset();
    }

    const risk_game::BoardMarks marks = risk_game::StepMarks(before, action);
    const auto full = [&] {
      return risk_game::RenderBoard(after, marks, BoardDetail::kFull);
    };
    const bool claimed_all =
        before.num_initial_placements_ + 1 == risk_game::kNumTerritories;
    const bool placed_all =
        before.initial_placement_ && !after.initial_placement_;
    const bool conquest =
        std::holds_alternative<risk_game::RollDiceAction>(action) &&
        before.queued_attack_.has_value() &&
        after.map_[before.queued_attack_->target].owner !=
            before.map_[before.queued_attack_->target].owner;
    bool new_attack = false;
    if (move != nullptr && move->attack_action_.has_value()) {
      const std::pair pair(move->attack_action_->source_,
                           move->attack_action_->target);
      new_attack = last_attack_ != pair;
      last_attack_ = pair;
    }

    view_.clear();
    if (const auto outcome = Outcome()) {
      view_ = Header(after) + ResultLine(*outcome) + full();
    } else if (TurnStart(before) || claimed_all || placed_all) {
      view_ = Header(after) + full();
    } else if (conquest || new_attack ||
               (fortify != nullptr && fortify->num_units > 1)) {
      view_ = risk_game::RenderBoard(after, marks, BoardDetail::kBand);
    }
  }

  const int max_rounds_;
  std::string initial_view_;
  std::string caption_;
  std::string view_;
  risk_game::TurnTally tally_;
  std::optional<std::pair<int, int>> last_attack_;
};

}  // namespace tournament_broker

#endif  // GAME_MCTS_PROBLEM_RISK_SESSION_H
