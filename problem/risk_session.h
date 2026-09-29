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

#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <utility>

#include "game_arena/referee/game_session.h"
#include "game_mcts/core/mcts/serialization.h"
#include "game_mcts/games/risk/risk_game.h"
#include "game_mcts/games/risk/risk_serialization.h"
#include "problem/risk_view.h"
#include "problem/session.h"

namespace tournament_broker {

class RiskSession final : public GameSessionImpl<risk_game::RiskState<2>> {
 public:
  using state_t = risk_game::RiskState<2>;

  // max_rounds <= 0: no cap.
  explicit RiskSession(int max_rounds, state_t initial = state_t{})
      : GameSessionImpl(std::move(initial)),
        max_rounds_(max_rounds),
        renderer_(max_rounds) {
    initial_view_ = ViewJson(State(), Rounds(State()), max_rounds_, {});
  }

  std::optional<GameOutcome> Outcome() const override {
    const RiskResult result = ResultOf(State(), max_rounds_);
    if (!result.over_) {
      return std::nullopt;
    }
    return result.draw_ ? GameOutcome{.is_draw = true}
                        : GameOutcome{.is_draw = false,
                                      .winning_player = result.winner_};
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
    return Steps().empty() ? initial_view_ : renderer_.view();
  }

  std::string RenderLastStep() const override { return renderer_.caption(); }

 private:
  void OnStep(const state_t &before) {
    traits::action_proto_t proto;
    proto.ParseFromString(Steps().back().action_bytes);
    renderer_.Render(before, traits::ActionFromProto(proto), State());
  }

  const int max_rounds_;
  std::string initial_view_;
  StepRenderer renderer_;
};

}  // namespace tournament_broker

#endif  // GAME_MCTS_PROBLEM_RISK_SESSION_H
