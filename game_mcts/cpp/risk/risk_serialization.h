#ifndef RISK_GAME_AI_CPP_RISK_RISK_SERIALIZATION_H
#define RISK_GAME_AI_CPP_RISK_RISK_SERIALIZATION_H

#include <cstddef>

#include "game_mcts/cpp/risk/risk.pb.h"
#include "game_mcts/cpp/risk/risk_game.h"
#include "game_mcts/cpp/mcts/serialization.h"

// Opts RiskState<NUM_PLAYERS> into mcts serialization: states convert to/from
// proto::RiskState, actions to/from proto::RiskAction. FromProto validates
// (num_players vs the template arity, territory count, per-field ranges) and
// CHECK-fails on malformed input.
namespace mcts {

template <std::size_t NUM_PLAYERS>
struct GameSerializationTraits<risk_game::RiskState<NUM_PLAYERS>> {
  static constexpr bool kEnabled = true;
  using state_proto_t = risk_game::proto::RiskState;
  using action_proto_t = risk_game::proto::RiskAction;

  static auto StateToProto(const risk_game::RiskState<NUM_PLAYERS> &state)
      -> state_proto_t;
  static auto StateFromProto(const state_proto_t &proto)
      -> risk_game::RiskState<NUM_PLAYERS>;
  static auto ActionToProto(const risk_game::RiskAction &action)
      -> action_proto_t;
  static auto ActionFromProto(const action_proto_t &proto)
      -> risk_game::RiskAction;
};

static_assert(SerializableGame<risk_game::RiskState<2>>);
static_assert(SerializableGame<risk_game::RiskState<3>>);

}  // namespace mcts

#endif  // RISK_GAME_AI_CPP_RISK_RISK_SERIALIZATION_H
