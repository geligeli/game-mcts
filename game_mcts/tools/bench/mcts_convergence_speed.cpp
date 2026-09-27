
#include <random>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/log/globals.h"
#include "absl/log/initialize.h"
#include "absl/log/log.h"
#include "game_mcts/common/fitters/simple_fitters.h"
#include "game_mcts/core/mcts/mcts.inl"
#include "game_mcts/games/tictactoe/tictactoe.h"
#include "game_mcts/tools/viz/visualisations.h"

ABSL_FLAG(std::string, output, "/dev/stderr",
          "Output file for the plot (default: /dev/stderr)");
ABSL_FLAG(int, port, 0, "Port to serve the plot on (0 = save to file only)");

int main(int argc, char **argv) {
  absl::InitializeLog();
  absl::SetStderrThreshold(absl::LogSeverityAtLeast::kInfo);
  absl::ParseCommandLine(argc, argv);

  tictactoe::TicTacToe game;
  mcts::MctsRunner runner(game);
  std::mt19937 gen(0);
  std::vector<double> errors;
  std::vector<double> iterations;

  LOG(INFO) << "Running MCTS iterations...";
  auto picker = mcts::MctsNodePicker<tictactoe::TicTacToe>(gen);
  for (int i = 0; i < 100000; ++i) {
    runner.OneIteration(picker, gen);
    if (i % 100 == 0 && i > 5000) {
      float estimated_value =
          static_cast<float>(runner.node_storage[0].total_value[0]) /
          runner.node_storage[0].num_visits;
      float true_value =
          0.0f;  // optimal play from both sides results in a draw
      float error = std::abs(estimated_value - true_value);
      errors.push_back(error);
      iterations.push_back(static_cast<double>(i));
    }
  }

  LOG(INFO) << "Fitting convergence curves...";

  // Fit both exponential and power law
  auto exp_result = fitters::fit_exponential(iterations, errors);
  auto pow_result = fitters::fit_power_law(iterations, errors);
  auto best_fit = fitters::choose_best_fit(iterations, errors);

  // Log the results
  if (exp_result.valid_) {
    LOG(INFO) << "Exponential fit: y = " << exp_result.a_ << " * exp("
              << exp_result.b_ << " * x), R² = " << exp_result.r_squared_;
  }
  if (pow_result.valid_) {
    LOG(INFO) << "Power law fit: y = " << pow_result.a_ << " * x^"
              << pow_result.alpha_ << ", R² = " << pow_result.r_squared_;
  }

  std::string best_fit_name;
  switch (best_fit) {
    case fitters::FitType::EXPONENTIAL:
      best_fit_name = "Exponential";
      LOG(INFO) << "Best fit: EXPONENTIAL";
      break;
    case fitters::FitType::POWER_LAW:
      best_fit_name = "Power Law";
      LOG(INFO) << "Best fit: POWER_LAW";
      break;
    case fitters::FitType::NONE:
      best_fit_name = "None";
      LOG(WARNING) << "No valid fit found";
      break;
  }

  // Generate fitted values
  std::vector<double> exp_fitted(iterations.size());
  std::vector<double> pow_fitted(iterations.size());

  for (size_t i = 0; i < iterations.size(); ++i) {
    if (exp_result.valid_) {
      exp_fitted[i] = exp_result.a_ * std::exp(exp_result.b_ * iterations[i]);
    }
    if (pow_result.valid_) {
      pow_fitted[i] =
          pow_result.a_ * std::pow(iterations[i], pow_result.alpha_);
    }
  }

  // Create visualization
  visualisations::Figure fig;
  fig.SetTitle("MCTS Convergence Analysis (Best fit: " + best_fit_name + ")");
  fig.SetLogY(true);

  // Plot actual errors
  fig.Scatter(iterations, errors);

  switch (best_fit) {
    case fitters::FitType::EXPONENTIAL: {
      std::string label = "Exponential Fit: " + std::to_string(exp_result.a_) +
                          " * exp(" + std::to_string(exp_result.b_) + " * x)";
      fig.Plot(iterations, exp_fitted, label);
      break;
    }
    case fitters::FitType::POWER_LAW: {
      std::string label = "Power Law Fit: " + std::to_string(pow_result.a_) +
                          " * x^" + std::to_string(pow_result.alpha_);

      fig.Plot(iterations, pow_fitted, label);
      break;
    }
    case fitters::FitType::NONE:
      break;
  }
  // Save or serve the plot
  int port = absl::GetFlag(FLAGS_port);
  if (port > 0) {
    LOG(INFO) << "Serving plot on port " << port;
    fig.Serve(port);
  } else {
    std::string output_file = absl::GetFlag(FLAGS_output);
    LOG(INFO) << "Saving plot to " << output_file;
    fig.Save(output_file);
  }
}
