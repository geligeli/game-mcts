#ifndef GAME_MCTS_GAME_MCTS_GAMES_RISK_RISK_SERIALIZATION_H
#define GAME_MCTS_GAME_MCTS_GAMES_RISK_RISK_SERIALIZATION_H

#include <cstddef>

#include "game_mcts/core/mcts/serialization.h"
#include "game_mcts/games/risk/risk.pb.h"
#include "game_mcts/games/risk/risk_game.h"

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

  static state_proto_t StateToProto(
      const risk_game::RiskState<NUM_PLAYERS> &state);
  static risk_game::RiskState<NUM_PLAYERS> StateFromProto(
      const state_proto_t &proto);
  static action_proto_t ActionToProto(const risk_game::RiskAction &action);
  static risk_game::RiskAction ActionFromProto(const action_proto_t &proto);
};

static_assert(SerializableGame<risk_game::RiskState<2>>);
static_assert(SerializableGame<risk_game::RiskState<3>>);

}  // namespace mcts

#endif  // GAME_MCTS_GAME_MCTS_GAMES_RISK_RISK_SERIALIZATION_H
