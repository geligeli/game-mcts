#include "game_mcts/games/risk/risk_render.h"

#include <cstdlib>
#include <string_view>

namespace risk_game {

namespace {

constexpr int kBoardWidth = 80;
constexpr int kTextWidth = 3;
// A longer arrow keeps its ends: the first and last few cells say enough.
constexpr std::size_t kMaxArrowCells = 12;

// Rows are about twice as tall as columns are wide.
std::string_view Glyph(int dx, int dy) {
  const int across = std::abs(dx);
  const int down = 2 * std::abs(dy);
  if (across > 2 * down) {
    return dx > 0 ? "→" : "←";
  }
  if (down > 2 * across) {
    return dy > 0 ? "↓" : "↑";
  }
  if (dx > 0) {
    return dy > 0 ? "↘" : "↗";
  }
  return dy > 0 ? "↙" : "↖";
}

bool OnACounter(std::span<const CellPos> counters, int row, int col) {
  return std::ranges::any_of(counters, [&](const CellPos &counter) {
    return counter.row_ == row && col >= counter.col_ &&
           col < counter.col_ + counter.width_;
  });
}

// The cells of an arrow between two troop counters, around every counter.
std::vector<BoardOverlay> ArrowCells(const CellPos &from, const CellPos &to,
                                     std::span<const CellPos> counters) {
  std::vector<BoardOverlay> cells;
  if (from.row_ < 0 || to.row_ < 0) {
    return cells;
  }
  const auto add = [&](int row, int col, std::string_view glyph) {
    if (col >= 0 && col < kBoardWidth && !OnACounter(counters, row, col)) {
      cells.push_back({.row_ = row, .col_ = col, .glyph_ = glyph});
    }
  };
  const int x0 = from.col_ + from.width_ / 2;
  const int x1 = to.col_ + to.width_ / 2;
  const int dx = x1 - x0;
  const int dy = to.row_ - from.row_;

  // Across the map's edge (Alaska to Kamchatka): leave by the near edge,
  // arrive from the far one.
  if (std::abs(dx) > kBoardWidth / 2) {
    const int step = dx > 0 ? -1 : 1;
    const std::string_view glyph = step > 0 ? "→" : "←";
    const int leave = step < 0 ? from.col_ - 1 : from.col_ + from.width_;
    const int arrive = step < 0 ? to.col_ + to.width_ : to.col_ - 1;
    for (int k = 0; k < 3; ++k) {
      add(from.row_, leave + k * step, glyph);
      add(to.row_, arrive - k * step, glyph);
    }
    return cells;
  }

  const std::string_view glyph = Glyph(dx, dy);
  const int steps = std::max(std::abs(dx), std::abs(dy));
  for (int k = 1; k < steps; ++k) {
    add(from.row_ + (dy * k + (dy >= 0 ? steps / 2 : -steps / 2)) / steps,
        x0 + (dx * k + (dx >= 0 ? steps / 2 : -steps / 2)) / steps, glyph);
  }
  if (cells.size() > kMaxArrowCells) {
    cells.erase(cells.begin() + kMaxArrowCells / 2,
                cells.end() - kMaxArrowCells / 2);
  }
  return cells;
}

}  // namespace

std::string RenderMarkedBoard(std::span<const uint8_t> colors,
                              std::span<const int> troops,
                              const BoardMarks &marks) {
  static const std::span<const AsciiBoardSegmentT> segments =
      GetAsciiBoardTemplate(kBoardWidth, kTextWidth);
  static const std::array<CellPos, 43> counters = CounterPositions(segments);
  if (segments.empty()) {
    return {};
  }
  std::string board = RenderAsciiBoard(segments, colors, troops);
  const auto at = [](int territory) {
    return counters[kCountryToTerritoryId[territory]];
  };
  if (marks.arrow_from_ >= 0 && marks.arrow_to_ >= 0) {
    board = OverlayCells(board, ArrowCells(at(marks.arrow_from_),
                                           at(marks.arrow_to_), counters));
  }
  return board;
}

}  // namespace risk_game
