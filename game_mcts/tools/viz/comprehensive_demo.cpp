#include <cmath>
#include <iostream>
#include <vector>

#include "game_mcts/tools/viz/visualisations.h"

int main(int argc, char* argv[]) {
  visualisations::Figure fig;

  // Generate some interesting data
  std::vector<double> x;
  std::vector<double> linear;
  std::vector<double> quadratic;
  std::vector<double> exponential;

  for (int i = 1; i <= 20; ++i) {
    x.push_back(i);
    linear.push_back(i * 10);
    quadratic.push_back(i * i);
    exponential.push_back(std::pow(1.5, i));
  }

  // Add multiple traces
  fig.Plot(x, linear);
  fig.Plot(x, quadratic);
  fig.Plot(x, exponential);

  // Configure the plot
  fig.SetLogY(true);
  fig.SetTitle("Comparison of Growth Functions (Log Scale)");

  // Check command line argument for mode
  if (argc > 1 && std::string(argv[1]) == "--serve") {
    std::cout << "Starting HTTP server mode..." << std::endl;
    std::cout << "Open http://localhost:8080 in your browser" << std::endl;
    fig.ServeOnce(8080);
  } else if (argc > 1 && std::string(argv[1]) == "--serve-continuous") {
    std::cout << "Starting continuous HTTP server mode..." << std::endl;
    std::cout << "Open http://localhost:8080 in your browser" << std::endl;
    std::cout << "Press Ctrl+C to stop" << std::endl;
    fig.Serve(8080);
  } else {
    std::cout << "Saving to file mode..." << std::endl;
    fig.Save("comprehensive_demo.html");
    std::cout << "\nTo serve via HTTP instead:" << std::endl;
    std::cout
        << "  bazel run //game_mcts/tools/viz:comprehensive_demo -- --serve"
        << std::endl;
    std::cout << "  bazel run //game_mcts/tools/viz:comprehensive_demo -- "
                 "--serve-continuous"
              << std::endl;
  }

  return 0;
}
