#include "cpp/risk/ascii_board.h"

#include <string>

namespace risk_game {
namespace {
struct RenderVisitor {
  std::string &result;
  std::span<const uint8_t> colors;
  std::span<const int> troop_counts;

  void operator()(const StringEntry &s) const { result.append(s.value); }

  void operator()(const TerritoryColor &c) const {
    const uint8_t color = colors[c.territory_id];
    if (color >= 8) {
      // Bright background variant: highlight regardless of owner color.
      result.append("\033[10");
      result.push_back(static_cast<char>('0' + color - 8));
    } else {
      result.append("\033[4");
      result.push_back(static_cast<char>('0' + color));
    }
    result.push_back('m');
  }

  void operator()(const TerritoryTroopCounter &t) const {
    int count = troop_counts[t.territory_id];
    auto s = std::to_string(count);
    while (s.size() < t.num_chars) {
      s.insert(s.begin(), '0');
    }
    if (s.size() > t.num_chars) {
      s = s.substr(s.size() - t.num_chars);
    }
    result.append(s);
  }
};
}  // namespace

auto RenderAsciiBoard(std::span<const AsciiBoardSegmentT> segments,
                      std::span<const uint8_t> colors,
                      std::span<const int> troop_counts) -> std::string {
  std::string result;
  RenderVisitor visitor{result, colors, troop_counts};
  for (const auto &segment : segments) {
    std::visit(visitor, segment);
  }
  return result;
}

}  // namespace risk_game
