#include <cmath>
#include <iostream>
#include <vector>

#include "game_mcts/cpp/visualisations/visualisations.h"

auto main(int argc, char* argv[]) -> int {
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
  fig.plot(x, linear);
  fig.plot(x, quadratic);
  fig.plot(x, exponential);

  // Configure the plot
  fig.set_log_y(true);
  fig.set_title("Comparison of Growth Functions (Log Scale)");

  // Check command line argument for mode
  if (argc > 1 && std::string(argv[1]) == "--serve") {
    std::cout << "Starting HTTP server mode..." << std::endl;
    std::cout << "Open http://localhost:8080 in your browser" << std::endl;
    fig.serve_once(8080);
  } else if (argc > 1 && std::string(argv[1]) == "--serve-continuous") {
    std::cout << "Starting continuous HTTP server mode..." << std::endl;
    std::cout << "Open http://localhost:8080 in your browser" << std::endl;
    std::cout << "Press Ctrl+C to stop" << std::endl;
    fig.serve(8080);
  } else {
    std::cout << "Saving to file mode..." << std::endl;
    fig.save("comprehensive_demo.html");
    std::cout << "\nTo serve via HTTP instead:" << std::endl;
    std::cout
        << "  bazel run //cpp/visualisations:comprehensive_demo -- --serve"
        << std::endl;
    std::cout << "  bazel run //cpp/visualisations:comprehensive_demo -- "
                 "--serve-continuous"
              << std::endl;
  }

  return 0;
}
