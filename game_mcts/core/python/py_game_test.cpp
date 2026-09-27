#include "game_mcts/core/python/py_game.h"

#include <gtest/gtest.h>

#include <array>
#include <memory>
#include <random>
#include <string>

#include "game_mcts/games/tictactoe/tictactoe.pb.h"
#include "game_mcts/games/tictactoe/tictactoe_serialization.h"

namespace {

using mcts::MakePyGame;
using mcts::PyGame;

// Minimal ChanceGame with structural (proto-free) "messages", proving that
// PyGameImpl needs only the concept surface: two players roll a d3 twice
// each (every node is a chance node), higher total wins, tie is a draw.
struct DiceDuel {
  using action_t = int;  // a die roll, 1..3
  std::array<int, 2> totals_ = {0, 0};
  int num_rolls_ = 0;  // players alternate; game ends after 4 rolls

  int current_player() const { return num_rolls_ < 4 ? -1 : 0; }
  bool is_chance_node() const { return num_rolls_ < 4; }
  action_t sample_chance_action(std::mt19937 &gen) const {
    return std::uniform_int_distribution<int>(1, 3)(gen);
  }
  void apply_action_in_place(const action_t &action) {
    totals_[num_rolls_ % 2] += action;
    ++num_rolls_;
  }
  DiceDuel apply_action(const action_t &action) const {
    DiceDuel next = *this;
    next.apply_action_in_place(action);
    return next;
  }
  mcts::game_state_t current_state() const {
    if (num_rolls_ < 4) {
      return mcts::ongoing_t{};
    }
    if (totals_[0] != totals_[1]) {
      return mcts::win_t{totals_[0] > totals_[1] ? 0 : 1};
    }
    return mcts::draw_t{};
  }
  bool is_valid_action(const action_t &action, std::string &reason) const {
    if (num_rolls_ >= 4) {
      reason = "game is over";
      return false;
    }
    if (action < 1 || action > 3) {
      reason = "die roll must be in 1..3";
      return false;
    }
    return true;
  }
};

// Structural stand-in for a protobuf message: 4-byte little-endian int.
struct IntProto {
  int value = 0;
  std::string SerializeAsString() const {
    std::string out(4, '\0');
    for (int i = 0; i < 4; ++i) {
      out[i] = static_cast<char>((value >> (8 * i)) & 0xFF);
    }
    return out;
  }
  bool ParseFromString(const std::string &s) {
    if (s.size() != 4) {
      return false;
    }
    value = 0;
    for (int i = 0; i < 4; ++i) {
      value |= static_cast<unsigned char>(s[i]) << (8 * i);
    }
    return true;
  }
  static std::string GetTypeName() { return "test.IntProto"; }
};

}  // namespace

namespace mcts {

template <>
struct GameSerializationTraits<DiceDuel> {
  static constexpr bool kEnabled = true;
  using state_proto_t = IntProto;
  using action_proto_t = IntProto;

  static IntProto StateToProto(const DiceDuel &state) {
    return {state.totals_[0] | (state.totals_[1] << 4) |
            (state.num_rolls_ << 8)};
  }
  static DiceDuel StateFromProto(const IntProto &proto) {
    DiceDuel state;
    state.totals_ = {proto.value & 0xF, (proto.value >> 4) & 0xF};
    state.num_rolls_ = proto.value >> 8;
    return state;
  }
  static IntProto ActionToProto(const DiceDuel::action_t &action) {
    return {action};
  }
  static DiceDuel::action_t ActionFromProto(const IntProto &proto) {
    return proto.value;
  }
};

static_assert(ProtoSerializableGame<DiceDuel>);
static_assert(ProtoSerializableGame<tictactoe::TicTacToe>);

}  // namespace mcts

namespace {

using mcts::GameSerializationTraits;

TEST(PyGameTest, TicTacToeScriptedGame) {
  using traits_t = GameSerializationTraits<tictactoe::TicTacToe>;
  std::unique_ptr<PyGame> game =
      MakePyGame<tictactoe::TicTacToe>(tictactoe::TicTacToe{}, /*seed=*/123);

  EXPECT_EQ(game->StateProtoType(), "tictactoe.proto.TicTacToeState");
  EXPECT_EQ(game->ActionProtoType(), "tictactoe.proto.TicTacToeAction");
  EXPECT_EQ(game->NumPlayers(), 2);
  EXPECT_EQ(game->current_player(), 0);
  EXPECT_FALSE(game->is_chance_node());
  EXPECT_FALSE(game->is_terminal());
  EXPECT_EQ(game->Result(), 0);
  EXPECT_EQ(game->winning_player(), -1);
  EXPECT_THROW(game->SampleChanceActionProto(), std::logic_error);

  // X plays 0, 1, 2 (top row); O plays 3, 4. X wins on the fifth move.
  const auto play = [&](int cell) {
    game->ApplyActionProto(traits_t::ActionToProto(cell).SerializeAsString());
  };
  play(0);
  EXPECT_EQ(game->current_player(), 1);
  play(3);
  play(1);
  play(4);
  EXPECT_FALSE(game->is_terminal());
  play(2);

  EXPECT_TRUE(game->is_terminal());
  EXPECT_EQ(game->Result(), 2);
  EXPECT_EQ(game->winning_player(), 0);

  // The state proto decodes to the same board (cells 0..4 occupied).
  tictactoe::proto::TicTacToeState proto;
  ASSERT_TRUE(proto.ParseFromString(game->StateProto()));
  const uint32_t occupied =
      (proto.board_state() & 0x1FF) | ((proto.board_state() >> 9) & 0x1FF);
  EXPECT_EQ(occupied, 0b11111);
}

TEST(PyGameTest, TicTacToeRejectsBadActions) {
  using traits_t = GameSerializationTraits<tictactoe::TicTacToe>;
  std::unique_ptr<PyGame> game =
      MakePyGame<tictactoe::TicTacToe>(tictactoe::TicTacToe{}, /*seed=*/1);

  const std::string cell0 = traits_t::ActionToProto(0).SerializeAsString();
  game->ApplyActionProto(cell0);

  // Occupied cell: referee reports it, apply throws.
  EXPECT_FALSE(game->CheckActionProto(cell0).empty());
  EXPECT_THROW(game->ApplyActionProto(cell0), std::invalid_argument);

  // Garbage bytes: parse failure, also reported.
  const std::string garbage("\xFF\xFE\xFD", 3);
  EXPECT_THROW(game->ApplyActionProto(garbage), std::invalid_argument);
  EXPECT_NE(game->CheckActionProto(garbage).find("cannot parse"),
            std::string::npos);
}

TEST(PyGameTest, ChanceGameWinDrawAndSampling) {
  using traits_t = GameSerializationTraits<DiceDuel>;

  {
    std::unique_ptr<PyGame> game = MakePyGame<DiceDuel>(DiceDuel{}, /*seed=*/7);
    EXPECT_TRUE(game->is_chance_node());
    EXPECT_EQ(game->current_player(), -1);

    // Sampled rolls are in range and advance the game when applied.
    const std::string roll = game->SampleChanceActionProto();
    IntProto parsed;
    ASSERT_TRUE(parsed.ParseFromString(roll));
    EXPECT_GE(parsed.value, 1);
    EXPECT_LE(parsed.value, 3);

    // Scripted rolls: player 0 wins 6 : 2.
    for (const int die : {3, 1, 3, 1}) {
      game->ApplyActionProto(traits_t::ActionToProto(die).SerializeAsString());
    }
    EXPECT_TRUE(game->is_terminal());
    EXPECT_EQ(game->Result(), 2);
    EXPECT_EQ(game->winning_player(), 0);
    EXPECT_FALSE(game->is_chance_node());
    EXPECT_THROW(game->SampleChanceActionProto(), std::logic_error);
    EXPECT_THROW(
        game->ApplyActionProto(traits_t::ActionToProto(1).SerializeAsString()),
        std::invalid_argument);
  }

  {
    std::unique_ptr<PyGame> game = MakePyGame<DiceDuel>(DiceDuel{}, /*seed=*/7);
    EXPECT_NE(
        game->CheckActionProto(traits_t::ActionToProto(5).SerializeAsString()),
        "");
    for (const int die : {2, 2, 2, 2}) {
      game->ApplyActionProto(traits_t::ActionToProto(die).SerializeAsString());
    }
    EXPECT_EQ(game->Result(), 1);  // draw
    EXPECT_EQ(game->winning_player(), -1);
  }
}

}  // namespace
