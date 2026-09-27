#include "game_mcts/games/risk/ascii/ascii_board.h"

#include <string>

namespace risk_game {
namespace {
struct RenderVisitor {
  std::string &result_;
  std::span<const uint8_t> colors_;
  std::span<const int> troop_counts_;

  void operator()(const StringEntry &s) const { result_.append(s.value); }

  void operator()(const TerritoryColor &c) const {
    const uint8_t color = colors_[c.territory_id_];
    if (color >= 8) {
      // Bright background variant: highlight regardless of owner color.
      result_.append("\033[10");
      result_.push_back(static_cast<char>('0' + color - 8));
    } else {
      result_.append("\033[4");
      result_.push_back(static_cast<char>('0' + color));
    }
    result_.push_back('m');
  }

  void operator()(const TerritoryTroopCounter &t) const {
    int count = troop_counts_[t.territory_id_];
    auto s = std::to_string(count);
    while (s.size() < t.num_chars_) {
      s.insert(s.begin(), '0');
    }
    if (s.size() > t.num_chars_) {
      s = s.substr(s.size() - t.num_chars_);
    }
    result_.append(s);
  }
};
}  // namespace

std::string RenderAsciiBoard(std::span<const AsciiBoardSegmentT> segments,
                             std::span<const uint8_t> colors,
                             std::span<const int> troop_counts) {
  std::string result;
  RenderVisitor visitor{result, colors, troop_counts};
  for (const auto &segment : segments) {
    std::visit(visitor, segment);
  }
  return result;
}

}  // namespace risk_game
