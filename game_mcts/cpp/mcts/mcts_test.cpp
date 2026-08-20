#include "game_mcts/cpp/mcts/mcts.inl"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cassert>
#include <fstream>
#include <optional>
#include <random>

#include "game_mcts/cpp/tictactoe/tictactoe.h"

namespace mcts {

TEST(MctsTest, BasicSelect) {
  MctsRunner<tictactoe::TicTacToe> runner(tictactoe::TicTacToe{});

  std::mt19937 gen;
  static_assert(!is_chance_game_v<tictactoe::TicTacToe>);
  auto picker = mcts::MctsNodePicker<tictactoe::TicTacToe>(gen);

  runner.path.push_back(0);
  ASSERT_NE(runner.select(picker), 0);

  std::cerr << runner << std::endl;
}

// TEST(MctsTest, BasicSelectWithPicker) {
//   MctsRunner<tictactoe::TicTacToe> runner1(tictactoe::TicTacToe{});
//   MctsRunner<tictactoe::TicTacToe> runner2(tictactoe::TicTacToe{});

//   std::mt19937 gen;
//   auto picker = mcts::MctsNodePicker<tictactoe::TicTacToe>(gen);
//   runner1.path.push_back(0);
//   std::size_t index_0 = runner1.select(picker);

//   std::mt19937 gen2(0);
//   runner2.path.push_back(0);
//   std::size_t index_1 = runner2.select(gen2);
//   ASSERT_EQ(index_0, index_1);
// }

// TEST(MctsTest, SelectAndPickerAreEquivalent) {
//   MctsRunner<tictactoe::TicTacToe> runner1(tictactoe::TicTacToe{});
//   MctsRunner<tictactoe::TicTacToe> runner2(tictactoe::TicTacToe{});

//   std::mt19937 gen;
//   std::mt19937 picker_gen;
//   auto picker = mcts::MctsNodePicker<tictactoe::TicTacToe>(picker_gen);
//   for (int i = 0; i < 500; ++i) {
//     runner1.OneIteration(gen);
//     runner2.OneIteration(picker, picker_gen);
//     ASSERT_EQ(runner1.path, runner2.path);

//     for (std::size_t j = 0; j < runner1.path.size(); ++j) {
//       ASSERT_EQ(runner1.node_storage[runner1.path[j]],
//                 runner2.node_storage[runner2.path[j]])
//           << "Mismatch at iteration " << i << " at path index " << j;
//     }
//   }
// }

TEST(MctsTest, OneIteration) {
  MctsRunner<tictactoe::TicTacToe> runner(tictactoe::TicTacToe{});
  std::mt19937 gen;
  auto picker = mcts::MctsNodePicker<tictactoe::TicTacToe>(gen);
  runner.OneIteration(picker, gen);
  std::cerr << runner << std::endl;
}

TEST(MctsTest, MultipleIterations) {
  MctsRunner<tictactoe::TicTacToe> runner(
      tictactoe::TicTacToe{}.apply_action(0));
  std::mt19937 gen;
  auto picker = mcts::MctsNodePicker<tictactoe::TicTacToe>(gen);
  for (int i = 0; i < 1000; ++i) {
    runner.OneIteration(picker, gen);
  }

  std::ofstream html_file("/tmp/test.html");
  PlotHtmlGraph(html_file, runner.node_storage,
                [](std::ostream &os, const tictactoe::TicTacToe &state) {
                  os << "<pre>" << state << "</pre>";
                });
}

TEST(MctsTest, MultipleIterationsCornerStart) {
  tictactoe::TicTacToe game;
  game = game.apply_action(0);
  game = game.apply_action(1);
  game = game.apply_action(2);
  game = game.apply_action(4);
  game = game.apply_action(5);
  /*
    X | O | X
   -----------
      | O | X
   -----------
      |   |
  */
  MctsRunner<tictactoe::TicTacToe> runner(game);
  std::mt19937 gen;
  auto picker = mcts::MctsNodePicker<tictactoe::TicTacToe>(gen);
  for (int i = 0; i < 100; ++i) {
    runner.OneIteration(picker, gen);
  }
  ASSERT_EQ(runner.best_action(), 7);
}

struct TerminalRootGame {
  using action_t = int;
  auto current_state() const -> mcts::game_state_t { return mcts::draw_t{}; }
  auto current_player() const -> int { return 0; }
  auto apply_action(action_t) const -> TerminalRootGame { return *this; }
  auto valid_moves() const -> mcts::VectorLegalActionSet<action_t> {
    return mcts::VectorLegalActionSet<action_t>{};
  }
  auto is_valid_action(const action_t &, std::string &) const -> bool {
    return true;
  }
};

static_assert(mcts::Game<TerminalRootGame>);

TEST(MctsTest, TerminalRootDoesNotExpand) {
  MctsRunner runner(TerminalRootGame{});
  std::mt19937 gen;
  auto picker = mcts::MctsNodePicker<TerminalRootGame>(gen);
  runner.OneIteration(picker, gen);

  ASSERT_EQ(runner.node_storage.size(), 1u);
  const auto &root = runner.node_storage[0];
  LOG(INFO) << runner;
  EXPECT_EQ(root.num_visits, 1);
  EXPECT_TRUE(root.children.empty());
  EXPECT_TRUE(is_terminal(root.current_game_state));
}

struct TreeGame {
  using action_t = int;
  struct Node {
    mcts::game_state_t state = mcts::ongoing_t{};
    int player_turn = 0;
    size_t left = 0;
    size_t right = 0;
  };

  size_t current_node_index_{0};

  /*
    Game tree structure:
           0
          / \
         /   \
        1     2
       / \   / \
      3   4 5   6
      |   | |   |
      W0  D D  W1
  */
  static constexpr std::array<Node, 7> kGameTree = {
      Node{mcts::ongoing_t{}, 0, 1, 2},  // 0
      Node{mcts::ongoing_t{}, 1, 3, 4},  // 1
      Node{mcts::ongoing_t{}, 1, 5, 6},  // 2

      Node{mcts::win_t{0}, 0, 0, 0},  // 3
      Node{mcts::draw_t{}, 0, 0, 0},  // 4
      Node{mcts::draw_t{}, 0, 0, 0},  // 5
      Node{mcts::win_t{1}, 0, 0, 0},  // 6
  };

  auto current_state() const -> mcts::game_state_t {
    assert(current_node_index_ < kGameTree.size());
    return kGameTree[current_node_index_].state;
  }

  auto current_player() const -> int {
    assert(current_node_index_ < kGameTree.size());
    return kGameTree[current_node_index_].player_turn;
  }

  auto apply_action(int action) const -> TreeGame {
    assert(action == 0 || action == 1);
    return TreeGame{action == 0 ? kGameTree[current_node_index_].left
                                : kGameTree[current_node_index_].right};
  }

  auto valid_moves() const -> mcts::ArrayLegalActionSet<2, action_t> {
    return mcts::ArrayLegalActionSet<2, action_t>{{0, 1}, 2};
  }

  auto is_valid_action(const action_t &, std::string &) const -> bool {
    return true;
  }
};

auto operator<<(std::ostream &os, const TreeGame &game) -> std::ostream & {
  os << "TreeGame(node_index=" << game.current_node_index_ << ", state=";
  std::visit(
      [&](const auto &state) {
        using T = std::decay_t<decltype(state)>;
        if constexpr (std::is_same_v<T, mcts::ongoing_t>) {
          os << "ongoing";
        } else if constexpr (std::is_same_v<T, mcts::draw_t>) {
          os << "draw";
        } else if constexpr (std::is_same_v<T, mcts::win_t>) {
          os << "win(player=" << state.winning_player << ")";
        }
      },
      game.current_state());
  os << ")";
  return os;
}

static_assert(mcts::Game<TreeGame>);

TEST(MctsTest, SampleGame) {
  MctsRunner runner(TreeGame{});
  std::mt19937 gen;
  auto picker = mcts::MctsNodePicker<TreeGame>(gen);
  for (int i = 0; i < 500; ++i) {
    runner.OneIteration(picker, gen);
  }
  LOG(INFO) << runner;

  runner.OneIteration(picker, gen);

  EXPECT_EQ(runner.best_action(), 0);
  std::ofstream html_file("/tmp/tree_game.html");
  PlotHtmlGraph(html_file, runner.node_storage,
                [](std::ostream &os, const TreeGame &state) {
                  os << "<pre>" << state << "</pre>";
                });
}

struct OrderedActionSet {
  using action_t = int;
  std::array<action_t, 2> actions{};
  std::size_t size = 0;
  auto operator<=>(const OrderedActionSet &other) const = default;

  template <class GameState, class Generator>
  auto next(const GameState &, Generator &) -> std::optional<action_t> {
    if (size == 0) {
      return std::nullopt;
    }
    --size;
    return actions[size];
  }

  auto empty() const -> bool { return size == 0; }
  auto begin() const { return actions.begin(); }
  auto end() const { return actions.begin() + size; }
};

struct ImmediateOutcomeGame {
  using action_t = int;
  enum class Node { kRoot, kWinForPlayer0, kWinForPlayer1 };

  Node node = Node::kRoot;

  auto current_state() const -> mcts::game_state_t {
    switch (node) {
      case Node::kRoot:
        return mcts::ongoing_t{};
      case Node::kWinForPlayer0:
        return mcts::win_t{0};
      case Node::kWinForPlayer1:
        return mcts::win_t{1};
    }
    return mcts::draw_t{};
  }

  auto current_player() const -> int { return 0; }

  auto apply_action(action_t action) const -> ImmediateOutcomeGame {
    if (node != Node::kRoot) {
      return *this;
    }
    if (action == 0) {
      return ImmediateOutcomeGame{Node::kWinForPlayer0};
    }
    return ImmediateOutcomeGame{Node::kWinForPlayer1};
  }

  auto valid_moves() const -> OrderedActionSet {
    if (node == Node::kRoot) {
      return OrderedActionSet{{0, 1}, 2};
    }
    return OrderedActionSet{{0, 0}, 0};
  }

  auto is_valid_action(const action_t &, std::string &) const -> bool {
    return true;
  }
};

static_assert(mcts::Game<ImmediateOutcomeGame>);
static_assert(mcts::ActionGenerator<OrderedActionSet, ImmediateOutcomeGame>);

TEST(MctsTest, ExpandsOneChildPerIteration) {
  MctsRunner runner(ImmediateOutcomeGame{});
  std::mt19937 gen(0);
  auto picker = mcts::MctsNodePicker<ImmediateOutcomeGame>(gen);

  runner.OneIteration(picker, gen);

  ASSERT_EQ(runner.node_storage.size(), 2u);
  const auto &root = runner.node_storage[0];
  ASSERT_EQ(root.children.size(), 1u);
  EXPECT_EQ(root.children[0].action, 1);
}

TEST(MctsTest, BackpropagatesWinsAndLosses) {
  MctsRunner runner(ImmediateOutcomeGame{});
  std::mt19937 gen(0);
  auto picker = mcts::MctsNodePicker<ImmediateOutcomeGame>(gen);

  runner.OneIteration(picker, gen);
  runner.OneIteration(picker, gen);

  const auto &root = runner.node_storage[0];
  ASSERT_EQ(root.children.size(), 2u);
  EXPECT_EQ(root.num_visits, 2);
  EXPECT_FLOAT_EQ(root.total_value[0], 0.0f);
  EXPECT_FLOAT_EQ(root.total_value[1], 0.0f);

  const auto find_child = [&](int action) -> const auto & {
    auto it =
        std::find_if(root.children.begin(), root.children.end(),
                     [&](const auto &child) { return child.action == action; });
    EXPECT_NE(it, root.children.end());
    return runner.node_storage[it->node_index];
  };

  // Per-player values: action 0 wins for player 0, action 1 for player 1.
  EXPECT_FLOAT_EQ(find_child(0).total_value[0], 1.0f);
  EXPECT_FLOAT_EQ(find_child(0).total_value[1], -1.0f);
  EXPECT_FLOAT_EQ(find_child(1).total_value[0], -1.0f);
  EXPECT_FLOAT_EQ(find_child(1).total_value[1], 1.0f);
}

TEST(MctsTest, PrefersWinningBranch) {
  MctsRunner runner(ImmediateOutcomeGame{});
  std::mt19937 gen(0);
  auto picker = mcts::MctsNodePicker<ImmediateOutcomeGame>(gen);

  for (int i = 0; i < 50; ++i) {
    runner.OneIteration(picker, gen);
  }

  EXPECT_EQ(runner.best_action(), 0);
}

}  // namespace mcts
