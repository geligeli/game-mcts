#include "game_mcts/cpp/mcts/mcts.h"

namespace mcts {

auto ucb_value(float total_value, int num_visits, int parent_visits) -> float {
  if (parent_visits == 0 || num_visits == 0) {
    return std::numeric_limits<float>::infinity();
  }
  return (total_value / num_visits) +
         std::sqrt(2.0f * std::log(parent_visits) / num_visits);
}

auto GameStateToString(const mcts::game_state_t& state) -> std::string {
  return std::visit(
      overloaded{[](const ongoing_t&) -> std::string { return "Ongoing"; },
                 [](const draw_t&) -> std::string { return "Draw"; },
                 [](const win_t& w) -> std::string {
                   return "Win by player " + std::to_string(w.winning_player);
                 }},
      state);
}

auto WriteHtmlGraphPrefix(std::ostream& os) -> void {
  os << R"HTML(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8" />
<title>MCTS Tree</title>
<style>
:root {
  --bg: #f6f1ea;
  --node-bg: #ffffff;
  --text: #1f2933;
  --accent: #ff6b35;
  --edge: #738091;
}
* {
  box-sizing: border-box;
}
body {
  font-family: "Space Mono", "Fira Code", monospace;
  background: linear-gradient(135deg, var(--bg), #fff);
  color: var(--text);
  margin: 0;
  padding: 32px;
}
.controls {
  display: flex;
  gap: 12px;
  margin-bottom: 20px;
}
button {
  background: var(--accent);
  border: none;
  color: #fff;
  padding: 8px 16px;
  font-size: 14px;
  border-radius: 20px;
  cursor: pointer;
  transition: transform 120ms ease;
}
button:hover {
  transform: translateY(-1px);
}
.mcts-tree {
  background: var(--node-bg);
  border-radius: 16px;
  padding: 24px;
  box-shadow: 0 12px 24px rgba(31, 41, 51, 0.12);
}
.mcts-tree details {
  border-left: 2px solid var(--edge);
  margin: 8px 0 8px 12px;
  padding-left: 12px;
}
.mcts-tree summary {
  cursor: pointer;
  list-style: none;
  position: relative;
  padding-left: 18px;
  font-weight: 600;
}
.mcts-tree summary::before {
  content: "+";
  position: absolute;
  left: 0;
  top: 0;
  color: var(--accent);
}
.mcts-tree details[open] > summary::before {
  content: "-";
}
.node-body {
  margin-top: 8px;
  padding: 12px 16px;
  border: 1px solid rgba(115,128,145,0.35);
  border-radius: 12px;
  background: rgba(255,255,255,0.6);
}
.stat-line {
  font-size: 13px;
  margin-bottom: 4px;
}
.leaf {
  font-style: italic;
  color: var(--edge);
}
.children {
  margin-top: 12px;
  display: flex;
  flex-direction: column;
  gap: 8px;
}
.child {
  border-top: 1px dashed rgba(115,128,145,0.4);
  padding-top: 8px;
}
.edge-label {
  font-size: 12px;
  color: var(--accent);
  letter-spacing: 0.04em;
  text-transform: uppercase;
  margin-bottom: 4px;
}
.empty-state {
  font-style: italic;
}
</style>
</head>
<body>
<div class="controls">
  <button onclick="expandAll()">Expand All</button>
  <button onclick="collapseAll()">Collapse All</button>
</div>
<div class="mcts-tree">
)HTML";
}

auto WriteHtmlGraphSuffix(std::ostream& os) -> void {
  os << R"HTML(</div>
<script>
function setAll(open) {
  document.querySelectorAll('.mcts-tree details').forEach((node) => {
    if (open) {
      node.setAttribute('open', 'open');
    } else {
      node.removeAttribute('open');
    }
  });
}
function expandAll() {
  setAll(true);
}
function collapseAll() {
  setAll(false);
}
</script>
</body>
</html>
)HTML";
}

}  // namespace mcts