#include <iostream>
#include <vector>

#include "game_mcts/tools/viz/visualisations.h"

int main() {
  visualisations::Figure fig;

  std::vector<double> x = {1, 2, 3, 4, 5};
  std::vector<double> y = {10, 100, 1000, 10000, 100000};

  // Add a line plot
  fig.Plot(x, y);

  // Add a scatter plot on top
  fig.Scatter(x, y);

  // Configure axes
  fig.SetLogY(true);
  fig.SetTitle("Logarithmic Growth - HTTP Server Demo");

  // Serve via HTTP (will serve once and then stop)
  std::cout << "Open your browser to view the plot" << std::endl;
  fig.ServeOnce(8080);

  return 0;
}
