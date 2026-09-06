#ifndef GAME_MCTS_GAME_MCTS_CPP_VISUALISATIONS_VISUALISATIONS_H
#define GAME_MCTS_GAME_MCTS_CPP_VISUALISATIONS_VISUALISATIONS_H

#include <cmath>
#include <sstream>
#include <string>
#include <vector>

namespace visualisations {

// Helper function to convert a vector to JSON array string
template <typename T>
auto vector_to_json(const std::vector<T>& vec) -> std::string {
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
  std::vector<double> x;
  std::vector<double> y;
  std::string mode;  // "lines", "markers", "lines+markers"
  std::string name;  // Trace name for legend

  Trace(std::vector<double> x_vals, std::vector<double> y_vals, std::string m,
        std::string n = "")
      : x(std::move(x_vals)),
        y(std::move(y_vals)),
        mode(std::move(m)),
        name(std::move(n)) {}
};

class Figure {
 private:
  std::vector<Trace> traces_;
  bool log_x_ = false;
  bool log_y_ = false;
  std::string title_;

  // Generate the complete HTML content
  std::string generate_html() const;

 public:
  Figure() = default;

  // plot(y) - X is index (0, 1, 2, ...)
  void plot(const std::vector<double>& y, const std::string& name = "");

  // plot(x, y) - X vs Y line plot
  void plot(const std::vector<double>& x, const std::vector<double>& y,
            const std::string& name = "");

  // scatter(x, y) - markers only, no lines
  void scatter(const std::vector<double>& x, const std::vector<double>& y,
               const std::string& name = "");

  // Set logarithmic scale for X axis
  void set_log_x(bool enable);

  // Set logarithmic scale for Y axis
  void set_log_y(bool enable);

  // Set chart title
  void set_title(const std::string& title);

  // Set the name of a trace by index (0-based)
  void set_trace_name(size_t index, const std::string& name);

  // Save the plot to an HTML file
  void save(const std::string& filename) const;

  // Serve the plot via HTTP on the specified port
  // Serves once and then stops
  void serve_once(int port = 8080) const;

  // Serve the plot via HTTP on the specified port
  // Serves continuously until interrupted (Ctrl+C)
  void serve(int port = 8080) const;
};

}  // namespace visualisations

#endif  // GAME_MCTS_GAME_MCTS_CPP_VISUALISATIONS_VISUALISATIONS_H
