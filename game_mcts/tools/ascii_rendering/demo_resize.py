#!/usr/bin/env python3
"""Demo: displays the best-fitting Risk board ASCII template on terminal resize.

Click territories to toggle their highlight color. Resize to switch board size.
"""

import os
import re
import select
import signal
import sys
import termios
import tty
from string import Template

TEMPLATE_DIR = os.path.join(os.path.dirname(__file__), "templates")
TEXT_WIDTH = 3
ANSI_RE = re.compile(r"\x1b\[[0-9;]*m")
RAW_COLOR_RE = re.compile(r"\033\[4\$\{COLOR_(\d+)\}m")
RAW_UNITS_RE = re.compile(r"\$\{UNITS_(\d+)\}")
RAW_ESC_RE = re.compile(r"\033\[[0-9;]*m")
HIGHLIGHT_COLOR = "1"  # red background


def load_raw_templates():
    """Load raw templates keyed by tile width, plus precomputed territory maps."""
    templates = {}
    for fname in sorted(os.listdir(TEMPLATE_DIR)):
        if not fname.endswith(".txt"):
            continue
        width = int(fname.replace(".txt", "").split("_")[1])
        with open(os.path.join(TEMPLATE_DIR, fname)) as f:
            raw = f.read()
        templates[width] = raw
    return templates


def measure_vis_width(rendered):
    return max(len(ANSI_RE.sub("", line)) for line in rendered.split("\n"))


def build_territory_map(raw_template):
    """Parse raw template to map (row, col) -> territory index."""
    tmap = {}
    for row, line in enumerate(raw_template.split("\n")):
        col = 0
        current = 0
        i = 0
        while i < len(line):
            m = RAW_COLOR_RE.match(line, i)
            if m:
                current = int(m.group(1))
                i = m.end()
                continue
            m = RAW_UNITS_RE.match(line, i)
            if m:
                for _ in range(TEXT_WIDTH):
                    tmap[(row, col)] = current
                    col += 1
                i = m.end()
                continue
            if line[i] == "\033":
                m2 = RAW_ESC_RE.match(line, i)
                if m2:
                    i = m2.end()
                    continue
            tmap[(row, col)] = current
            col += 1
            i += 1
    return tmap


def render_template(raw_template, highlighted):
    unit_dict = {f"UNITS_{i}": f"{i:0{TEXT_WIDTH}}" for i in range(1, 43)}
    colors = {f"COLOR_{i}": HIGHLIGHT_COLOR if i in highlighted else str(i % 6)
              for i in range(43)}
    if 0 not in highlighted:
        colors["COLOR_0"] = "4"
    return Template(raw_template).substitute({**unit_dict, **colors})


def truncate_line(line, cols):
    """Truncate a rendered line to fit in cols visible characters."""
    vis = 0
    out = []
    parts = ANSI_RE.split(line)
    codes = ANSI_RE.findall(line)
    for idx, part in enumerate(parts):
        if vis >= cols:
            break
        remaining = cols - vis
        if len(part) > remaining:
            out.append(part[:remaining])
            vis += remaining
        else:
            out.append(part)
            vis += len(part)
        if idx < len(codes):
            out.append(codes[idx])
    out.append("\033[0m")
    return "".join(out)


class State:
    def __init__(self, raw_templates):
        self.raw_templates = raw_templates
        # Precompute territory maps and visible widths
        self.tmaps = {}
        self.vis_widths = {}
        for w, raw in raw_templates.items():
            self.tmaps[w] = build_territory_map(raw)
            rendered = render_template(raw, set())
            self.vis_widths[w] = measure_vis_width(rendered)
        self.highlighted = set()
        self.current_width = None
        self.current_tmap = None

    def pick_width(self, cols):
        best = min(self.vis_widths)
        for w, vw in sorted(self.vis_widths.items()):
            if vw <= cols:
                best = w
        return best

    def display(self):
        cols, rows = os.get_terminal_size()
        w = self.pick_width(cols)
        self.current_width = w
        self.current_tmap = self.tmaps[w]

        rendered = render_template(self.raw_templates[w], self.highlighted)
        vis_w = self.vis_widths[w]
        lines = rendered.split("\n")
        if vis_w > cols:
            lines = [truncate_line(l, cols) for l in lines]
        lines = lines[:rows - 2]

        n_highlighted = len(self.highlighted - {0})
        status = f"[{cols}x{rows}, board w={w}] click territory to highlight ({n_highlighted} selected, q to quit)"
        lines.append(status)

        buf = "\033[?25l\033[H"
        for line in lines:
            buf += line + "\033[K\r\n"
        buf += "\033[J\033[?25h"
        sys.stdout.write(buf)
        sys.stdout.flush()

    def handle_click(self, row, col):
        if self.current_tmap is None:
            return
        # row/col from mouse are 1-based
        territory = self.current_tmap.get((row - 1, col - 1), None)
        if territory is None or territory == 0:
            return
        if territory in self.highlighted:
            self.highlighted.discard(territory)
        else:
            self.highlighted.add(territory)
        self.display()


def main():
    raw_templates = load_raw_templates()
    if not raw_templates:
        print(f"No templates found in {TEMPLATE_DIR}", file=sys.stderr)
        sys.exit(1)

    state = State(raw_templates)
    old_termios = termios.tcgetattr(sys.stdin)

    def cleanup():
        sys.stdout.write("\033[?1006l\033[?1000l")  # disable mouse
        sys.stdout.write("\033[0m\n")
        sys.stdout.flush()
        termios.tcsetattr(sys.stdin, termios.TCSADRAIN, old_termios)

    try:
        tty.setraw(sys.stdin.fileno())
        # Enable SGR extended mouse tracking
        sys.stdout.write("\033[?1000h\033[?1006h")
        sys.stdout.flush()

        signal.signal(signal.SIGWINCH, lambda *_: state.display())
        state.display()

        # SGR mouse: \033[<btn;col;rowM (press) or \033[<btn;col;rowm (release)
        sgr_mouse_re = re.compile(rb"\033\[<(\d+);(\d+);(\d+)([Mm])")

        buf = b""
        while True:
            r, _, _ = select.select([sys.stdin], [], [], 0.5)
            if not r:
                continue
            buf += os.read(sys.stdin.fileno(), 256)

            # Check for 'q' quit
            if b"q" in buf or b"Q" in buf or b"\x03" in buf:  # q or Ctrl-C
                break

            # Parse all complete SGR mouse sequences
            while True:
                m = sgr_mouse_re.search(buf)
                if not m:
                    # Keep only tail that could be a partial escape
                    esc_pos = buf.rfind(b"\033")
                    if esc_pos >= 0:
                        buf = buf[esc_pos:]
                    else:
                        buf = b""
                    break
                btn = int(m.group(1))
                col = int(m.group(2))
                row = int(m.group(3))
                press = m.group(4) == b"M"
                buf = buf[m.end():]
                # Button 0 press = left click
                if btn == 0 and press:
                    state.handle_click(row, col)

    finally:
        cleanup()


if __name__ == "__main__":
    main()
