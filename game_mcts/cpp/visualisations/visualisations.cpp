#include "game_mcts/cpp/visualisations/visualisations.h"

#include <fstream>
#include <iostream>

#include "game_mcts/cpp/visualisations/http_server.h"

namespace visualisations {

auto escape_json_string(const std::string& str) -> std::string {
  std::ostringstream oss;
  for (char c : str) {
    switch (c) {
      case '"':
        oss << "\\\"";
        break;
      case '\\':
        oss << "\\\\";
        break;
      case '\b':
        oss << "\\b";
        break;
      case '\f':
        oss << "\\f";
        break;
      case '\n':
        oss << "\\n";
        break;
      case '\r':
        oss << "\\r";
        break;
      case '\t':
        oss << "\\t";
        break;
      default:
        oss << c;
        break;
    }
  }
  return oss.str();
}

auto Figure::generate_html() const -> std::string {
  std::ostringstream html;

  // HTML boilerplate
  html << R"(<!DOCTYPE html>
<html>
<head>
    <meta charset="utf-8">
    <title>)"
       << escape_json_string(title_) << R"(</title>
    <script src="https://cdn.plot.ly/plotly-latest.min.js"></script>
</head>
<body>
    <div id="plot" style="width:100%;height:100vh;"></div>
    <script>
        var data = [)";

  // Generate traces
  bool first_trace = true;
  for (const auto& trace : traces_) {
    if (!first_trace) html << ",";
    html << "\n            {";
    html << "\n                x: " << vector_to_json(trace.x) << ",";
    html << "\n                y: " << vector_to_json(trace.y) << ",";
    html << "\n                mode: '" << trace.mode << "',";
    html << "\n                type: 'scatter'";
    if (!trace.name.empty()) {
      html << ",\n                name: '" << escape_json_string(trace.name)
           << "'";
    }
    html << "\n            }";
    first_trace = false;
  }

  html << "\n        ];\n";

  // Generate layout
  html << "        var layout = {\n";
  if (!title_.empty()) {
    html << "            title: '" << escape_json_string(title_) << "',\n";
  }
  html << "            xaxis: { type: '" << (log_x_ ? "log" : "linear")
       << "' },\n";
  html << "            yaxis: { type: '" << (log_y_ ? "log" : "linear")
       << "' }\n";
  html << "        };\n";

  // Plotly render call
  html << "        Plotly.newPlot('plot', data, layout);\n";
  html << R"(    </script>
</body>
</html>)";

  return html.str();
}

void Figure::plot(const std::vector<double>& y, const std::string& name) {
  std::vector<double> x(y.size());
  for (size_t i = 0; i < y.size(); ++i) {
    x[i] = static_cast<double>(i);
  }
  traces_.emplace_back(std::move(x), y, "lines", name);
}

void Figure::plot(const std::vector<double>& x, const std::vector<double>& y,
                  const std::string& name) {
  traces_.emplace_back(x, y, "lines", name);
}

void Figure::scatter(const std::vector<double>& x, const std::vector<double>& y,
                     const std::string& name) {
  traces_.emplace_back(x, y, "markers", name);
}

void Figure::set_log_x(bool enable) { log_x_ = enable; }

void Figure::set_log_y(bool enable) { log_y_ = enable; }

void Figure::set_title(const std::string& title) { title_ = title; }

void Figure::set_trace_name(size_t index, const std::string& name) {
  if (index < traces_.size()) {
    traces_[index].name = name;
  } else {
    std::cerr << "Error: Trace index " << index << " out of range.\n";
  }
}

void Figure::save(const std::string& filename) const {
  std::ofstream file(filename);
  if (!file.is_open()) {
    std::cerr << "Error: Could not open file " << filename << " for writing.\n";
    return;
  }
  file << generate_html();
  file.close();
  std::cout << "Plot saved to " << filename << std::endl;
}

void Figure::serve_once(int port) const {
  Server server(port);
  server.serve_once([this]() { return generate_html(); });
}

void Figure::serve(int port) const {
  Server server(port);
  server.serve([this]() { return generate_html(); });
}

}  // namespace visualisations
