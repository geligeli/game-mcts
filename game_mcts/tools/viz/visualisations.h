#ifndef GAME_MCTS_GAME_MCTS_TOOLS_VIZ_VISUALISATIONS_H
#define GAME_MCTS_GAME_MCTS_TOOLS_VIZ_VISUALISATIONS_H

#include <cmath>
#include <sstream>
#include <string>
#include <vector>

namespace visualisations {

// Helper function to convert a vector to JSON array string
template <typename T>
std::string vector_to_json(const std::vector<T>& vec) {
  std::ostringstream oss;
  oss << "[";
  bool first = true;
  for (const auto& val : vec) {
    if (!first) oss << ",";

    // Handle NaN and Inf - convert to null for valid JSON
    if constexpr (std::is_floating_point_v<T>) {
      if (std::isnan(val) || std::isinf(val)) {
        oss << "null";
      } else {
        oss << val;
      }
    } else {
      oss << val;
    }

    first = false;
  }
  oss << "]";
  return oss.str();
}

// Helper function to escape strings for JSON
std::string escape_json_string(const std::string& str);

// Trace structure to hold data for each plot/scatter
struct Trace {
  std::vector<double> x_;
  std::vector<double> y_;
  std::string mode_;  // "lines", "markers", "lines+markers"
  std::string name_;  // Trace name for legend

  Trace(std::vector<double> x_vals, std::vector<double> y_vals, std::string m,
        std::string n = "")
      : x_(std::move(x_vals)),
        y_(std::move(y_vals)),
        mode_(std::move(m)),
        name_(std::move(n)) {}
};

class Figure {
 private:
  std::vector<Trace> traces_;
  bool log_x_ = false;
  bool log_y_ = false;
  std::string title_;

  // Generate the complete HTML content
  std::string GenerateHtml() const;

 public:
  Figure() = default;

  // plot(y) - X is index (0, 1, 2, ...)
  void Plot(const std::vector<double>& y, const std::string& name = "");

  // plot(x, y) - X vs Y line plot
  void Plot(const std::vector<double>& x, const std::vector<double>& y,
            const std::string& name = "");

  // scatter(x, y) - markers only, no lines
  void Scatter(const std::vector<double>& x, const std::vector<double>& y,
               const std::string& name = "");

  // Set logarithmic scale for X axis
  void SetLogX(bool enable);

  // Set logarithmic scale for Y axis
  void SetLogY(bool enable);

  // Set chart title
  void SetTitle(const std::string& title);

  // Set the name of a trace by index (0-based)
  void SetTraceName(size_t index, const std::string& name);

  // Save the plot to an HTML file
  void Save(const std::string& filename) const;

  // Serve the plot via HTTP on the specified port
  // Serves once and then stops
  void ServeOnce(int port = 8080) const;

  // Serve the plot via HTTP on the specified port
  // Serves continuously until interrupted (Ctrl+C)
  void Serve(int port = 8080) const;
};

}  // namespace visualisations

#endif  // GAME_MCTS_GAME_MCTS_TOOLS_VIZ_VISUALISATIONS_H
