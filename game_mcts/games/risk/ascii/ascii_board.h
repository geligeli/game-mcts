#ifndef RISK_GAME_AI_CPP_RISK_ASCII_BOARD_H
#define RISK_GAME_AI_CPP_RISK_ASCII_BOARD_H
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <variant>

namespace risk_game {

inline constexpr int kNumTerritories = 42;

struct StringEntry {
  std::string_view value;
};

struct TerritoryColor {
  uint8_t territory_id;
};

struct TerritoryTroopCounter {
  int troop_count;
  uint8_t territory_id;
  uint8_t num_chars;
};

using AsciiBoardSegmentT =
    std::variant<StringEntry, TerritoryColor, TerritoryTroopCounter>;

// Per-cell territory ID grid (row-major, 0 = background, 1..42 = territory).
struct TerritoryMap {
  const uint8_t *data;  // row-major: data[row * cols + col]
  int rows;
  int cols;

  auto At(int row, int col) const -> uint8_t { return data[row * cols + col]; }
  explicit operator bool() const { return data != nullptr; }
};

// Metadata for a single pre-generated board template.
struct BoardInfo {
  int width;       // tile width (the generation parameter)
  int text_width;  // digits reserved for troop counts
  int vis_cols;    // visible columns when rendered
  int vis_rows;    // visible rows when rendered
};

// Mapping from Country enum index (0..41) to template territory ID (1..42).
// The Country enum is alphabetical; the template IDs follow COCO polygon order.
inline constexpr std::array<uint8_t, 42> kCountryToTerritoryId = {
    24, 1,  2,  40, 39, 20, 6,   // Afghanistan..Central_America
    29, 11, 36, 8,  7,  22, 12,  // China..Great_Britain
    9,  13, 33, 38, 32, 31, 28,  // Greenland..Kamchatka
    10, 23, 30, 35, 19, 16, 3,   // Madagascar..Northwest_Territory
    4,  41, 14, 26, 21, 34, 17,  // Ontario..Southern_Europe
    15, 25, 42, 37, 18, 5,  27,  // Ukraine..Yakutsk
};

// Render a board from segments, substituting colors and troop counts.
// |colors| is indexed by territory_id (0..42), values are ANSI color digits:
// 0-7 render a normal background, 8-15 render the bright variant of color-8
// (useful to highlight a territory regardless of its owner color).
// |troop_counts| is indexed by territory_id (1..42).
std::string RenderAsciiBoard(std::span<const AsciiBoardSegmentT> segments,
                             std::span<const uint8_t> colors,
                             std::span<const int> troop_counts);

// Look up a pre-generated board template by tile width and text width.
// Returns an empty span if no matching template exists.
std::span<const AsciiBoardSegmentT> GetAsciiBoardTemplate(int width,
                                                          int text_width);

// Get the territory cell map for a given template.
// Returns a null TerritoryMap if not found.
TerritoryMap GetTerritoryMap(int width, int text_width);

// List all available pre-generated board templates.
std::span<const BoardInfo> GetAvailableBoardTemplates();

}  // namespace risk_game

#endif  // RISK_GAME_AI_CPP_RISK_ASCII_BOARD_H
