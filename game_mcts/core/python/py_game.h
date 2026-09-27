#ifndef GAME_MCTS_GAME_MCTS_CORE_PYTHON_PY_GAME_H
#define GAME_MCTS_GAME_MCTS_CORE_PYTHON_PY_GAME_H

#include <cstdint>
#include <memory>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

#include "game_mcts/core/mcts/game_traits.h"
#include "game_mcts/core/mcts/serialization.h"

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
  virtual std::string StateProto() const = 0;
  // Full proto message names, for Python-side message class lookup.
  virtual std::string StateProtoType() const = 0;
  virtual std::string ActionProtoType() const = 0;

  // Applies one serialized action proto. Throws std::invalid_argument on
  // malformed bytes or on an action that is illegal in the current state.
  virtual void ApplyActionProto(const std::string &action_proto) = 0;
  // Referee check: empty string = legal, else a human-readable reason.
  virtual std::string CheckActionProto(
      const std::string &action_proto) const = 0;

  virtual int current_player() const = 0;  // -1 at chance nodes
  virtual int NumPlayers() const = 0;
  virtual bool is_chance_node() const = 0;
  // Draws a rules-defined chance action (dice, ...) as a serialized action
  // proto. The RNG is seeded at construction. Throws std::logic_error when
  // the game has no chance nodes or is not currently at one.
  virtual std::string SampleChanceActionProto() = 0;

  virtual bool is_terminal() const = 0;
  virtual int Result() const = 0;          // 0 ongoing, 1 draw, 2 win
  virtual int winning_player() const = 0;  // meaningful iff result==2
};

// PyGame over a concrete game type. Holds the game state and the chance RNG.
template <typename G>
  requires ProtoSerializableGame<G>
class PyGameImpl final : public PyGame {
 public:
  using traits_t = GameSerializationTraits<G>;

  explicit PyGameImpl(G state, std::uint32_t seed)
      : game_(std::move(state)), gen_(seed) {}

  std::string StateProto() const override {
    return traits_t::StateToProto(game_).SerializeAsString();
  }
  std::string StateProtoType() const override {
    return std::string(typename traits_t::state_proto_t{}.GetTypeName());
  }
  std::string ActionProtoType() const override {
    return std::string(typename traits_t::action_proto_t{}.GetTypeName());
  }

  void ApplyActionProto(const std::string &bytes) override {
    const typename G::action_t action = ParseAction(bytes);
    std::string reason;
    if (!game_.is_valid_action(action, reason)) {
      throw std::invalid_argument("illegal " + ActionProtoType() + ": " +
                                  reason);
    }
    if constexpr (InPlaceGame<G>) {
      game_.apply_action_in_place(action);
    } else {
      game_ = game_.apply_action(action);
    }
  }

  std::string CheckActionProto(const std::string &bytes) const override {
    typename traits_t::action_proto_t proto;
    if (!proto.ParseFromString(bytes)) {
      return "cannot parse " + ActionProtoType();
    }
    std::string reason;
    if (!game_.is_valid_action(traits_t::ActionFromProto(proto), reason)) {
      return reason;
    }
    return "";
  }

  int current_player() const override { return game_.current_player(); }
  int NumPlayers() const override { return static_cast<int>(num_players_v<G>); }

  bool is_chance_node() const override {
    if constexpr (ChanceGame<G>) {
      return game_.is_chance_node();
    } else {
      return false;
    }
  }

  std::string SampleChanceActionProto() override {
    if constexpr (ChanceGame<G>) {
      if (!game_.is_chance_node()) {
        throw std::logic_error("not at a chance node");
      }
      return traits_t::ActionToProto(game_.sample_chance_action(gen_))
          .SerializeAsString();
    } else {
      throw std::logic_error(ActionProtoType() + " has no chance nodes");
    }
  }

  bool is_terminal() const override {
    return mcts::is_terminal(game_.current_state());
  }
  int Result() const override {
    const auto state = game_.current_state();
    if (std::holds_alternative<win_t>(state)) {
      return 2;
    }
    if (std::holds_alternative<draw_t>(state)) {
      return 1;
    }
    return 0;
  }
  int winning_player() const override {
    if (const auto state = game_.current_state();
        std::holds_alternative<win_t>(state)) {
      return std::get<win_t>(state).winning_player;
    }
    return -1;
  }

 private:
  typename G::action_t ParseAction(const std::string &bytes) const {
    typename traits_t::action_proto_t proto;
    if (!proto.ParseFromString(bytes)) {
      throw std::invalid_argument("cannot parse " + ActionProtoType());
    }
    return traits_t::ActionFromProto(proto);
  }

  G game_;
  std::mt19937 gen_;
};

template <typename G>
  requires ProtoSerializableGame<G>
std::unique_ptr<PyGame> MakePyGame(G state, std::uint32_t seed) {
  return std::make_unique<PyGameImpl<G>>(std::move(state), seed);
}

template <typename G>
  requires ProtoSerializableGame<G>
std::unique_ptr<PyGame> MakePyGame() {
  return MakePyGame<G>(G{}, std::random_device{}());
}

}  // namespace mcts

#endif  // GAME_MCTS_GAME_MCTS_CORE_PYTHON_PY_GAME_H
