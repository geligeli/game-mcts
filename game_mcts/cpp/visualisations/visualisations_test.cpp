#include "game_mcts/cpp/visualisations/visualisations.h"

#include <vector>

auto main() -> int {
  visualisations::Figure fig;

  std::vector<double> x = {1, 2, 3, 4, 5};
  std::vector<double> y = {10, 100, 1000, 10000, 100000};

  // Add a line plot
  fig.plot(x, y);

  // Add a scatter plot on top
  fig.scatter(x, y);

  // Configure axes
  fig.set_log_y(true);
  fig.set_title("Logarithmic Growth");

  fig.save("plot.html");
  return 0;
}
