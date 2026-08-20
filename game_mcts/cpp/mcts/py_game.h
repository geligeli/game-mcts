#ifndef GAME_MCTS_GAME_MCTS_CPP_MCTS_PY_GAME_H
#define GAME_MCTS_GAME_MCTS_CPP_MCTS_PY_GAME_H

#include <cstdint>
#include <memory>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

#include "game_mcts/cpp/mcts/game_traits.h"
#include "game_mcts/cpp/mcts/serialization.h"

namespace mcts {

// Refinement of SerializableGame with the two extra proto-message operations
// the driving interface needs: parsing (the recorder only ever serializes)
// and type names (so the Python side can pick the right message class).
// Constrained structurally, so this header stays free of protobuf includes.
template <typename G>
concept ProtoSerializableGame =
    SerializableGame<G> &&
    requires(const std::string &bytes,
             GameSerializationTraits<G>::state_proto_t &state_proto,
             GameSerializationTraits<G>::action_proto_t &action_proto) {
      { state_proto.ParseFromString(bytes) } -> std::convertible_to<bool>;
      { action_proto.ParseFromString(bytes) } -> std::convertible_to<bool>;
      { state_proto.GetTypeName() } -> std::convertible_to<std::string_view>;
      { action_proto.GetTypeName() } -> std::convertible_to<std::string_view>;
    };

// Type-erased driving interface over any ProtoSerializableGame. Proto
// messages cross as serialized bytes: a game that can serialize its state
// and actions is fully drivable from a recording. This is the boundary the
// Python bindings (py_game_binding.h) expose; it deliberately knows neither
// pybind11 nor protobuf.
class PyGame {
 public:
  virtual ~PyGame() = default;

  // Serialized state proto of the current state.
  virtual auto state_proto() const -> std::string = 0;
  // Full proto message names, for Python-side message class lookup.
  virtual auto state_proto_type() const -> std::string = 0;
  virtual auto action_proto_type() const -> std::string = 0;

  // Applies one serialized action proto. Throws std::invalid_argument on
  // malformed bytes or on an action that is illegal in the current state.
  virtual void apply_action_proto(const std::string &action_proto) = 0;
  // Referee check: empty string = legal, else a human-readable reason.
  virtual auto check_action_proto(const std::string &action_proto) const
      -> std::string = 0;

  virtual auto current_player() const -> int = 0;  // -1 at chance nodes
  virtual auto num_players() const -> int = 0;
  virtual auto is_chance_node() const -> bool = 0;
  // Draws a rules-defined chance action (dice, ...) as a serialized action
  // proto. The RNG is seeded at construction. Throws std::logic_error when
  // the game has no chance nodes or is not currently at one.
  virtual auto sample_chance_action_proto() -> std::string = 0;

  virtual auto is_terminal() const -> bool = 0;
  virtual auto result() const -> int = 0;          // 0 ongoing, 1 draw, 2 win
  virtual auto winning_player() const -> int = 0;  // meaningful iff result==2
};

// PyGame over a concrete game type. Holds the game state and the chance RNG.
template <typename G>
  requires ProtoSerializableGame<G>
class PyGameImpl final : public PyGame {
 public:
  using traits_t = GameSerializationTraits<G>;

  explicit PyGameImpl(G state, std::uint32_t seed)
      : game_(std::move(state)), gen_(seed) {}

  auto state_proto() const -> std::string override {
    return traits_t::StateToProto(game_).SerializeAsString();
  }
  auto state_proto_type() const -> std::string override {
    return std::string(typename traits_t::state_proto_t{}.GetTypeName());
  }
  auto action_proto_type() const -> std::string override {
    return std::string(typename traits_t::action_proto_t{}.GetTypeName());
  }

  void apply_action_proto(const std::string &bytes) override {
    const typename G::action_t action = ParseAction(bytes);
    std::string reason;
    if (!game_.is_valid_action(action, reason)) {
      throw std::invalid_argument("illegal " + action_proto_type() + ": " +
                                  reason);
    }
    if constexpr (InPlaceGame<G>) {
      game_.apply_action_in_place(action);
    } else {
      game_ = game_.apply_action(action);
    }
  }

  auto check_action_proto(const std::string &bytes) const
      -> std::string override {
    typename traits_t::action_proto_t proto;
    if (!proto.ParseFromString(bytes)) {
      return "cannot parse " + action_proto_type();
    }
    std::string reason;
    if (!game_.is_valid_action(traits_t::ActionFromProto(proto), reason)) {
      return reason;
    }
    return "";
  }

  auto current_player() const -> int override { return game_.current_player(); }
  auto num_players() const -> int override {
    return static_cast<int>(num_players_v<G>);
  }

  auto is_chance_node() const -> bool override {
    if constexpr (ChanceGame<G>) {
      return game_.is_chance_node();
    } else {
      return false;
    }
  }

  auto sample_chance_action_proto() -> std::string override {
    if constexpr (ChanceGame<G>) {
      if (!game_.is_chance_node()) {
        throw std::logic_error("not at a chance node");
      }
      return traits_t::ActionToProto(game_.sample_chance_action(gen_))
          .SerializeAsString();
    } else {
      throw std::logic_error(action_proto_type() + " has no chance nodes");
    }
  }

  auto is_terminal() const -> bool override {
    return mcts::is_terminal(game_.current_state());
  }
  auto result() const -> int override {
    const auto state = game_.current_state();
    if (std::holds_alternative<win_t>(state)) {
      return 2;
    }
    if (std::holds_alternative<draw_t>(state)) {
      return 1;
    }
    return 0;
  }
  auto winning_player() const -> int override {
    if (const auto state = game_.current_state();
        std::holds_alternative<win_t>(state)) {
      return std::get<win_t>(state).winning_player;
    }
    return -1;
  }

 private:
  auto ParseAction(const std::string &bytes) const -> typename G::action_t {
    typename traits_t::action_proto_t proto;
    if (!proto.ParseFromString(bytes)) {
      throw std::invalid_argument("cannot parse " + action_proto_type());
    }
    return traits_t::ActionFromProto(proto);
  }

  G game_;
  std::mt19937 gen_;
};

template <typename G>
  requires ProtoSerializableGame<G>
auto MakePyGame(G state, std::uint32_t seed) -> std::unique_ptr<PyGame> {
  return std::make_unique<PyGameImpl<G>>(std::move(state), seed);
}

template <typename G>
  requires ProtoSerializableGame<G>
auto MakePyGame() -> std::unique_ptr<PyGame> {
  return MakePyGame<G>(G{}, std::random_device{}());
}

}  // namespace mcts

#endif  // GAME_MCTS_GAME_MCTS_CPP_MCTS_PY_GAME_H
