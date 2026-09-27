#include "game_mcts/games/risk/ascii/ascii_board.h"

#include <algorithm>
#include <string>
#include <vector>

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
// Bytes in the UTF-8 sequence |lead| starts.
int CodePointBytes(unsigned char lead) {
  return lead < 0x80 ? 1 : lead < 0xE0 ? 2 : lead < 0xF0 ? 3 : 4;
}

// Past an ESC [ ... letter sequence starting at |i|, or |i| if there is none.
std::size_t SkipEscape(std::string_view text, std::size_t i) {
  if (text[i] != '\x1b' || i + 1 >= text.size() || text[i + 1] != '[') {
    return i;
  }
  std::size_t end = i + 2;
  while (end < text.size() && !(text[end] >= '@' && text[end] <= '~')) {
    ++end;
  }
  return std::min(end + 1, text.size());
}

}  // namespace

std::array<CellPos, 43> CounterPositions(
    std::span<const AsciiBoardSegmentT> segments) {
  std::array<CellPos, 43> positions{};
  int row = 0;
  int col = 0;
  for (const AsciiBoardSegmentT &segment : segments) {
    if (const auto *text = std::get_if<StringEntry>(&segment)) {
      for (std::size_t i = 0; i < text->value.size();) {
        if (const std::size_t past = SkipEscape(text->value, i); past != i) {
          i = past;
        } else if (text->value[i] == '\n') {
          ++row;
          col = 0;
          ++i;
        } else {
          ++col;
          i += CodePointBytes(static_cast<unsigned char>(text->value[i]));
        }
      }
    } else if (const auto *counter =
                   std::get_if<TerritoryTroopCounter>(&segment)) {
      positions[counter->territory_id_] = {
          .row_ = row, .col_ = col, .width_ = counter->num_chars_};
      col += counter->num_chars_;
    }
  }
  return positions;
}

std::string OverlayCells(std::string_view board,
                         std::span<const BoardOverlay> overlay) {
  std::string out;
  out.reserve(board.size() + overlay.size() * 24);
  int row = 0;
  int col = 0;
  for (std::size_t i = 0; i < board.size();) {
    if (const std::size_t past = SkipEscape(board, i); past != i) {
      out.append(board.substr(i, past - i));
      i = past;
      continue;
    }
    if (board[i] == '\n') {
      out += '\n';
      ++row;
      col = 0;
      ++i;
      continue;
    }
    const int bytes = CodePointBytes(static_cast<unsigned char>(board[i]));
    const auto mark =
        std::ranges::find_if(overlay, [&](const BoardOverlay &cell) {
          return cell.row_ == row && cell.col_ == col;
        });
    if (mark != overlay.end()) {
      out.append("\x1b[1;97m").append(mark->glyph_).append("\x1b[22;39m");
    } else {
      out.append(board.substr(i, bytes));
    }
    ++col;
    i += bytes;
  }
  return out;
}

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
