#ifndef GAME_MCTS_GAME_MCTS_CPP_TICTACTOE_TICTACTOE_SERIALIZATION_H
#define GAME_MCTS_GAME_MCTS_CPP_TICTACTOE_TICTACTOE_SERIALIZATION_H

#include "game_mcts/cpp/mcts/serialization.h"
#include "game_mcts/cpp/tictactoe/tictactoe.h"
#include "game_mcts/cpp/tictactoe/tictactoe.pb.h"

// Minimal serialization trait for TicTacToe: the state is the 18-bit board
// plus the player to move, the action is the cell index 0..8. Doubles as
// proof that the trait is game-generic.
namespace mcts {

template <>
struct GameSerializationTraits<tictactoe::TicTacToe> {
  static constexpr bool kEnabled = true;
  using state_proto_t = tictactoe::proto::TicTacToeState;
  using action_proto_t = tictactoe::proto::TicTacToeAction;

  static auto StateToProto(const tictactoe::TicTacToe &state) -> state_proto_t {
    state_proto_t proto;
    proto.set_board_state(state.board_state);
    proto.set_current_player(state.current_player());
    return proto;
  }

  static auto StateFromProto(const state_proto_t &proto)
      -> tictactoe::TicTacToe {
    tictactoe::TicTacToe state;
    state.board_state = proto.board_state();
    state.current_player_ = proto.current_player();
    return state;
  }

  static auto ActionToProto(const tictactoe::TicTacToe::action_t &action)
      -> action_proto_t {
    action_proto_t proto;
    proto.set_cell(action);
    return proto;
  }

  static auto ActionFromProto(const action_proto_t &proto)
      -> tictactoe::TicTacToe::action_t {
    return proto.cell();
  }
};

static_assert(SerializableGame<tictactoe::TicTacToe>);

}  // namespace mcts

#endif  // GAME_MCTS_GAME_MCTS_CPP_TICTACTOE_TICTACTOE_SERIALIZATION_H
