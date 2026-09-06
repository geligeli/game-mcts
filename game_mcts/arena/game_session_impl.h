#ifndef GAME_MCTS_GAME_MCTS_ARENA_GAME_SESSION_IMPL_H
#define GAME_MCTS_GAME_MCTS_ARENA_GAME_SESSION_IMPL_H

// Adapts any mcts::SerializableGame to the arena's byte-level GameSession.
//
// This is the whole binding between this framework and the arena: everything
// the arena knows about a game_mcts game arrives through here, as serialized
// protos. It lives on this side of the seam because it is the mcts concepts,
// not the arena, that it is written against.

#include <memory>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <utility>

#include "absl/log/check.h"
#include "game_mcts/core/mcts/game_traits.h"
#include "game_mcts/core/mcts/serialization.h"
#include "game_mcts/tournament_server/referee/game_session.h"

namespace tournament_broker {

template <mcts::SerializableGame G>
class GameSessionImpl final : public GameSession {
 public:
  using traits = mcts::GameSerializationTraits<G>;

  explicit GameSessionImpl(G initial = G{}) : state_(std::move(initial)) {}

  auto SerializeState() const -> std::string override {
    return traits::StateToProto(state_).SerializeAsString();
  }

  auto CurrentPlayer() const -> int override { return state_.current_player(); }

  auto IsChanceNode() const -> bool override {
    if constexpr (mcts::ChanceGame<G>) {
      return state_.is_chance_node();
    } else {
      return false;
    }
  }

  void ApplyChanceAction(std::mt19937 &gen) override {
    if constexpr (mcts::ChanceGame<G>) {
      const typename G::action_t action = state_.sample_chance_action(gen);
      RecordStep(-1, traits::ActionToProto(action).SerializeAsString());
      state_ = state_.apply_action(action);
    } else {
      CHECK(false) << "ApplyChanceAction on a game without chance nodes";
    }
  }

  auto ApplySerializedAction(std::string_view bytes,
                             std::string *error) -> bool override {
    typename traits::action_proto_t action_proto;
    if (!action_proto.ParseFromArray(bytes.data(),
                                     static_cast<int>(bytes.size()))) {
      *error = "action bytes do not parse";
      return false;
    }
    const typename G::action_t action = traits::ActionFromProto(action_proto);
    if (!state_.is_valid_action(action, *error)) {
      return false;
    }
    RecordStep(state_.current_player(), std::string(bytes));
    state_ = state_.apply_action(action);
    return true;
  }

  auto Outcome() const -> std::optional<GameOutcome> override {
    const mcts::game_state_t state = state_.current_state();
    if (const auto *win = std::get_if<mcts::win_t>(&state)) {
      return GameOutcome{.is_draw = false,
                         .winning_player = win->winning_player};
    }
    if (std::get_if<mcts::draw_t>(&state) != nullptr) {
      return GameOutcome{.is_draw = true};
    }
    return std::nullopt;
  }

  // Typed access for game-specific code (tests, registry setup).
  auto state() const -> const G & { return state_; }

 private:
  G state_;
};

}  // namespace tournament_broker

#endif  // GAME_MCTS_GAME_MCTS_ARENA_GAME_SESSION_IMPL_H
