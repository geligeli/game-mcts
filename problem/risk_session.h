#ifndef GAME_MCTS_PROBLEM_RISK_SESSION_H
#define GAME_MCTS_PROBLEM_RISK_SESSION_H

// risk2 and risk3: Risk for two and for three, plus what a tournament needs
// on top of the rules.
//
// - Places. A seat knocked out places below every survivor, the last to go
//   best, so with three seats outlasting a rival counts.
// - A round cap. Risk only ends when one side owns the whole map, so passive
//   play never ends, and the referee's move cap settles a draw by thinking
//   time, which rewards the fastest staller. After max_rounds full rounds the
//   survivors place by territories, then armies; only an exact tie is left to
//   the referee.
// - A replay. Every step gets a caption (RenderLastStep()) and a view
//   (RenderState()): the board it left as a JSON snapshot (problem/risk_view.h)
//   with what it touched and its dice, which problem/risk_replay.js draws on
//   the map in the browser.

#include <cstddef>
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

template <size_t N>
class RiskSession final : public GameSessionImpl<risk_game::RiskState<N>> {
 public:
  using state_t = risk_game::RiskState<N>;
  using base_t = GameSessionImpl<state_t>;

  // max_rounds <= 0: no cap.
  explicit RiskSession(int max_rounds, state_t initial = state_t{})
      : base_t(std::move(initial)),
        max_rounds_(max_rounds),
        renderer_(max_rounds) {
    initial_view_ =
        ViewJson(this->State(), Rounds(this->State()), max_rounds_, {});
  }

  std::optional<GameOutcome> Outcome() const override {
    RiskResult result =
        ResultOf(this->State(), max_rounds_, renderer_.eliminated());
    if (!result.over_) {
      return std::nullopt;
    }
    return GameOutcome{std::move(result.places_)};
  }

  GameOutcome Standing() const override {
    return {ResultOf(this->State(), max_rounds_, renderer_.eliminated(),
                     /*over=*/true)
                .places_};
  }

  bool ApplySerializedAction(std::string_view bytes,
                             std::string *error) override {
    const state_t before = this->State();
    if (!base_t::ApplySerializedAction(bytes, error)) {
      return false;
    }
    OnStep(before);
    return true;
  }

  void ApplyChanceAction(std::mt19937 &gen) override {
    const state_t before = this->State();
    base_t::ApplyChanceAction(gen);
    OnStep(before);
  }

  std::string RenderState() const override {
    return this->Steps().empty() ? initial_view_ : renderer_.view();
  }

  std::string RenderLastStep() const override { return renderer_.caption(); }

 private:
  void OnStep(const state_t &before) {
    typename base_t::traits::action_proto_t proto;
    proto.ParseFromString(this->Steps().back().action_bytes);
    renderer_.Render(before, base_t::traits::ActionFromProto(proto),
                     this->State());
  }

  const int max_rounds_;
  std::string initial_view_;
  StepRenderer<N> renderer_;
};

}  // namespace tournament_broker

#endif  // GAME_MCTS_PROBLEM_RISK_SESSION_H
