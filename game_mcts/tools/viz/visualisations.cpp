#include "game_mcts/tools/viz/visualisations.h"

#include <fstream>
#include <iostream>

#include "game_mcts/tools/viz/http_server.h"

namespace visualisations {

std::string escape_json_string(const std::string& str) {
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

std::string Figure::GenerateHtml() const {
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
    html << "\n                x: " << vector_to_json(trace.x_) << ",";
    html << "\n                y: " << vector_to_json(trace.y_) << ",";
    html << "\n                mode: '" << trace.mode_ << "',";
    html << "\n                type: 'scatter'";
    if (!trace.name_.empty()) {
      html << ",\n                name: '" << escape_json_string(trace.name_)
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

void Figure::Plot(const std::vector<double>& y, const std::string& name) {
  std::vector<double> x(y.size());
  for (size_t i = 0; i < y.size(); ++i) {
    x[i] = static_cast<double>(i);
  }
  traces_.emplace_back(std::move(x), y, "lines", name);
}

void Figure::Plot(const std::vector<double>& x, const std::vector<double>& y,
                  const std::string& name) {
  traces_.emplace_back(x, y, "lines", name);
}

void Figure::Scatter(const std::vector<double>& x, const std::vector<double>& y,
                     const std::string& name) {
  traces_.emplace_back(x, y, "markers", name);
}

void Figure::SetLogX(bool enable) { log_x_ = enable; }

void Figure::SetLogY(bool enable) { log_y_ = enable; }

void Figure::SetTitle(const std::string& title) { title_ = title; }

void Figure::SetTraceName(size_t index, const std::string& name) {
  if (index < traces_.size()) {
    traces_[index].name_ = name;
  } else {
    std::cerr << "Error: Trace index " << index << " out of range.\n";
  }
}

void Figure::Save(const std::string& filename) const {
  std::ofstream file(filename);
  if (!file.is_open()) {
    std::cerr << "Error: Could not open file " << filename << " for writing.\n";
    return;
  }
  file << GenerateHtml();
  file.close();
  std::cout << "Plot saved to " << filename << std::endl;
}

void Figure::ServeOnce(int port) const {
  Server server(port);
  server.ServeOnce([this]() { return GenerateHtml(); });
}

void Figure::Serve(int port) const {
  Server server(port);
  server.Serve([this]() { return GenerateHtml(); });
}

}  // namespace visualisations
