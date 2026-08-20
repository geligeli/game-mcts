#ifndef GAME_MCTS_GAME_MCTS_CPP_MCTS_SERIALIZATION_H
#define GAME_MCTS_GAME_MCTS_CPP_MCTS_SERIALIZATION_H

#include <concepts>
#include <string>

#include "game_mcts/cpp/mcts/game_traits.h"

namespace mcts {

// Per-game opt-in serialization trait. The primary template serializes
// nothing; a game opts in by specializing this template (in a game-specific
// header, next to the proto it converts to/from) with:
//
//   static constexpr bool kEnabled = true;
//   using state_proto_t = ...;   // protobuf message type for the state
//   using action_proto_t = ...;  // protobuf message type for the action
//   static auto StateToProto(const G &) -> state_proto_t;
//   static auto StateFromProto(const state_proto_t &) -> G;
//   static auto ActionToProto(const typename G::action_t &) -> action_proto_t;
//   static auto ActionFromProto(const action_proto_t &) -> typename
//   G::action_t;
//
// This header deliberately stays free of protobuf includes: the trait is just
// a template, and only the specializations name concrete message types.
template <typename G>
struct GameSerializationTraits {
  static constexpr bool kEnabled = false;
};

// A Game whose serialization trait is enabled and whose four conversions have
// matching types. The proto types are only constrained structurally (they
// must be byte-serializable messages), keeping this header proto-free.
template <typename G>
concept SerializableGame =
    Game<G> &&
    requires(const G &state, const typename G::action_t &action,
             const GameSerializationTraits<G>::state_proto_t &state_proto,
             const GameSerializationTraits<G>::action_proto_t &action_proto) {
      requires GameSerializationTraits<G>::kEnabled;
      typename GameSerializationTraits<G>::state_proto_t;
      typename GameSerializationTraits<G>::action_proto_t;
      {
        GameSerializationTraits<G>::StateToProto(state)
      } -> std::same_as<typename GameSerializationTraits<G>::state_proto_t>;
      {
        GameSerializationTraits<G>::StateFromProto(state_proto)
      } -> std::same_as<G>;
      {
        GameSerializationTraits<G>::ActionToProto(action)
      } -> std::same_as<typename GameSerializationTraits<G>::action_proto_t>;
      {
        GameSerializationTraits<G>::ActionFromProto(action_proto)
      } -> std::same_as<typename G::action_t>;
      { action_proto.SerializeAsString() } -> std::convertible_to<std::string>;
    };

}  // namespace mcts

#endif  // GAME_MCTS_GAME_MCTS_CPP_MCTS_SERIALIZATION_H
