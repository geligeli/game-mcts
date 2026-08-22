#include <signal.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <termios.h>
#include <unistd.h>

#include <array>
#include <cstdio>
#include <cstring>
#include <set>
#include <string>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "cpp/risk/ascii_board.h"

ABSL_FLAG(int, text_width, 3, "Digits reserved for troop counts (2 or 3)");

namespace {

using namespace risk_game;

// ---------------------------------------------------------------------------
// Terminal helpers
// ---------------------------------------------------------------------------

struct TermSize {
  int cols;
  int rows;
};

auto GetTermSize() -> TermSize {
  struct winsize ws {};
  ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws);
  return {ws.ws_col, ws.ws_row};
}

struct RawMode {
  struct termios orig {};

  RawMode() {
    tcgetattr(STDIN_FILENO, &orig);
    struct termios raw = orig;
    cfmakeraw(&raw);
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
  }
  ~RawMode() { tcsetattr(STDIN_FILENO, TCSADRAIN, &orig); }
  RawMode(const RawMode &) = delete;
  auto operator=(const RawMode &) -> RawMode & = delete;
};

void EnableMouse() {
  // 1000h = basic mouse, 1006h = SGR extended coordinates
  [[maybe_unused]] auto r = write(STDOUT_FILENO, "\033[?1000h\033[?1006h", 16);
}

void DisableMouse() {
  [[maybe_unused]] auto r = write(STDOUT_FILENO, "\033[?1006l\033[?1000l", 16);
}

void WriteStr(const std::string &s) {
  [[maybe_unused]] auto r = write(STDOUT_FILENO, s.data(), s.size());
}

// ---------------------------------------------------------------------------
// Display state
// ---------------------------------------------------------------------------

class Display {
 public:
  explicit Display(int text_width) : text_width_(text_width) {
    for (const auto &info : GetAvailableBoardTemplates()) {
      if (info.text_width == text_width_) {
        infos_.push_back(info);
      }
    }
  }

  auto PickWidth(int cols) const -> int {
    int best = infos_.front().width;
    for (const auto &info : infos_) {
      if (info.vis_cols <= cols) {
        best = info.width;
      }
    }
    return best;
  }

  void Refresh() {
    auto ts = GetTermSize();
    int w = PickWidth(ts.cols);
    current_width_ = w;
    current_map_ = GetTerritoryMap(w, text_width_);

    auto segments = GetAsciiBoardTemplate(w, text_width_);

    std::array<uint8_t, 43> colors{};
    colors[0] = 4;  // blue background
    for (int i = 1; i <= kNumTerritories; ++i) {
      colors[i] = highlighted_.contains(i) ? 1 : (i % 6);
    }

    std::array<int, 43> troops{};
    for (int i = 1; i <= kNumTerritories; ++i) {
      troops[i] = i;
    }

    std::string rendered = RenderAsciiBoard(segments, colors, troops);

    // Build output: hide cursor, home, draw lines, clear rest, show cursor.
    std::string buf = "\033[?25l\033[H";

    int line_count = 0;
    size_t pos = 0;
    while (pos < rendered.size() && line_count < ts.rows - 2) {
      size_t nl = rendered.find('\n', pos);
      if (nl == std::string::npos) nl = rendered.size();
      buf.append(rendered, pos, nl - pos);
      buf += "\033[K\r\n";
      pos = nl + 1;
      ++line_count;
    }

    // Status line.
    int n = static_cast<int>(highlighted_.size());
    char status[128];
    snprintf(status, sizeof(status),
             "[%dx%d, board w=%d] click territory to highlight (%d selected,"
             " q to quit)",
             ts.cols, ts.rows, w, n);
    buf += status;
    buf += "\033[K";

    // Clear remaining lines and show cursor.
    buf += "\033[J\033[?25h";
    WriteStr(buf);
  }

  void HandleClick(int row, int col) {
    if (!current_map_) return;
    // Mouse coords are 1-based.
    int r = row - 1, c = col - 1;
    if (r < 0 || r >= current_map_.rows || c < 0 || c >= current_map_.cols) {
      return;
    }
    uint8_t territory = current_map_.At(r, c);
    if (territory == 0) return;
    if (highlighted_.contains(territory)) {
      highlighted_.erase(territory);
    } else {
      highlighted_.insert(territory);
    }
    Refresh();
  }

 private:
  int text_width_;
  int current_width_ = 0;
  TerritoryMap current_map_{};
  std::set<uint8_t> highlighted_;
  std::vector<BoardInfo> infos_;
};

// Global pointer so the signal handler can reach it.
Display *g_display = nullptr;

void SigwinchHandler(int) {
  if (g_display) g_display->Refresh();
}

// ---------------------------------------------------------------------------
// Input parsing
// ---------------------------------------------------------------------------

// SGR mouse: \033[<btn;col;rowM  (press) or m (release)
struct MouseEvent {
  int btn, col, row;
  bool press;
};

auto ParseSGRMouse(const char *buf, int len, int &consumed,
                   MouseEvent &ev) -> bool {
  // Look for \033[< ... M or m
  for (int i = 0; i + 3 < len; ++i) {
    if (buf[i] != '\033' || buf[i + 1] != '[' || buf[i + 2] != '<') continue;
    int j = i + 3;
    int btn = 0, col = 0, row = 0;
    while (j < len && buf[j] >= '0' && buf[j] <= '9')
      btn = btn * 10 + (buf[j++] - '0');
    if (j >= len || buf[j] != ';') continue;
    ++j;
    while (j < len && buf[j] >= '0' && buf[j] <= '9')
      col = col * 10 + (buf[j++] - '0');
    if (j >= len || buf[j] != ';') continue;
    ++j;
    while (j < len && buf[j] >= '0' && buf[j] <= '9')
      row = row * 10 + (buf[j++] - '0');
    if (j >= len) return false;
    if (buf[j] != 'M' && buf[j] != 'm') continue;
    ev = {btn, col, row, buf[j] == 'M'};
    consumed = j + 1;
    return true;
  }
  return false;
}

}  // namespace

auto main(int argc, char **argv) -> int {
  absl::ParseCommandLine(argc, argv);
  int text_width = absl::GetFlag(FLAGS_text_width);

  Display display(text_width);
  g_display = &display;

  RawMode raw_mode;
  EnableMouse();

  struct sigaction sa {};
  sa.sa_handler = SigwinchHandler;
  sa.sa_flags = SA_RESTART;
  sigaction(SIGWINCH, &sa, nullptr);

  display.Refresh();

  char buf[512];
  int buf_len = 0;
  bool running = true;

  while (running) {
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(STDIN_FILENO, &fds);
    struct timeval tv {
      0, 500000
    };  // 0.5s
    int ret = select(STDIN_FILENO + 1, &fds, nullptr, nullptr, &tv);
    if (ret <= 0) continue;

    int n = read(STDIN_FILENO, buf + buf_len, sizeof(buf) - buf_len - 1);
    if (n <= 0) break;
    buf_len += n;
    buf[buf_len] = '\0';

    // Check for quit keys.
    for (int i = 0; i < buf_len; ++i) {
      if (buf[i] == 'q' || buf[i] == 'Q' || buf[i] == '\x03') {
        running = false;
        break;
      }
    }
    if (!running) break;

    // Parse mouse events.
    while (buf_len > 0) {
      int consumed = 0;
      MouseEvent ev{};
      if (!ParseSGRMouse(buf, buf_len, consumed, ev)) {
        // Keep tail that could be a partial escape.
        int esc = -1;
        for (int i = buf_len - 1; i >= 0; --i) {
          if (buf[i] == '\033') {
            esc = i;
            break;
          }
        }
        if (esc >= 0 && esc < buf_len) {
          memmove(buf, buf + esc, buf_len - esc);
          buf_len -= esc;
        } else {
          buf_len = 0;
        }
        break;
      }
      // Consume processed bytes.
      memmove(buf, buf + consumed, buf_len - consumed);
      buf_len -= consumed;
      // Left-click press.
      if (ev.btn == 0 && ev.press) {
        display.HandleClick(ev.row, ev.col);
      }
    }
  }

  DisableMouse();
  WriteStr("\033[0m\n");
  g_display = nullptr;
  return 0;
}
