#include "game_mcts/games/risk/ascii/ascii_board.h"

#include <gtest/gtest.h>

#include <array>
#include <iostream>
#include <string>

namespace risk_game {

TEST(AsciiBoardTest, RenderBoard48) {
  auto segments = GetAsciiBoardTemplate(48, 2);
  ASSERT_FALSE(segments.empty());

  std::array<uint8_t, 43> colors{};
  colors[0] = 4;  // background = blue
  for (int i = 1; i <= 42; ++i) {
    colors[i] = i % 6;
  }

  std::array<int, 43> troop_counts{};
  for (int i = 1; i <= 42; ++i) {
    troop_counts[i] = i;
  }

  std::string result = RenderAsciiBoard(segments, colors, troop_counts);
  ASSERT_FALSE(result.empty());

  // Check that every territory troop count appears in the output.
  for (int i = 1; i <= 42; ++i) {
    char buf[8];
    snprintf(buf, sizeof(buf), "%02d", i);
    EXPECT_NE(result.find(buf), std::string::npos)
        << "Missing troop count for territory " << i;
  }

  // Check ANSI color codes are present.
  EXPECT_NE(result.find("\033[4"), std::string::npos);

  std::cerr << result << std::endl;
}

TEST(AsciiBoardTest, MissingTemplateReturnsEmpty) {
  auto segments = GetAsciiBoardTemplate(9999, 9999);
  EXPECT_TRUE(segments.empty());
}

TEST(AsciiBoardTest, AllTemplatesNonEmpty) {
  // Spot-check a few known widths.
  for (int w : {48, 60, 80, 100, 120}) {
    for (int tw : {2, 3}) {
      auto segments = GetAsciiBoardTemplate(w, tw);
      // Some widths may not exist, but if they do, they should have segments.
      if (!segments.empty()) {
        EXPECT_GT(segments.size(), 10)
            << "Template " << w << "x" << tw << " suspiciously small";
      }
    }
  }
}

// The replay's arrows start and end at the troop counters, so every territory
// needs one inside the visible board.
TEST(AsciiBoardTest, EveryTerritoryHasACounterOnTheBoard) {
  const auto segments = GetAsciiBoardTemplate(80, 3);
  ASSERT_FALSE(segments.empty());
  const std::array<CellPos, 43> counters = CounterPositions(segments);
  for (int id = 1; id <= kNumTerritories; ++id) {
    EXPECT_GE(counters[id].row_, 0) << "territory id " << id;
    EXPECT_LT(counters[id].row_, 25) << "territory id " << id;
    EXPECT_GE(counters[id].col_, 0) << "territory id " << id;
    EXPECT_LE(counters[id].col_ + counters[id].width_, 80)
        << "territory id " << id;
    EXPECT_EQ(counters[id].width_, 3) << "territory id " << id;
  }
}

// An overlay replaces exactly one visible cell and leaves the colours around
// it alone.
TEST(AsciiBoardTest, OverlayReplacesOneCell) {
  const std::string board = "\x1b[44mab\x1b[0m\n\x1b[41mcd\x1b[0m\n";
  const std::array<BoardOverlay, 1> arrow = {
      BoardOverlay{.row_ = 1, .col_ = 1, .glyph_ = "→"}};
  EXPECT_EQ(OverlayCells(board, arrow),
            "\x1b[44mab\x1b[0m\n\x1b[41mc\x1b[1;97m→\x1b[22;39m\x1b[0m\n");
}

}  // namespace risk_game
