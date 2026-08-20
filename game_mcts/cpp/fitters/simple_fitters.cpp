#include "game_mcts/cpp/fitters/simple_fitters.h"

namespace fitters {

namespace {
// Helper function to compute linear regression on transformed data
// Returns: {slope, intercept, r_squared}
auto linear_regression(const std::vector<double>& x,
                       const std::vector<double>& y)
    -> std::tuple<double, double, double> {
  if (x.size() != y.size() || x.size() < 2) {
    throw std::invalid_argument("Invalid input sizes for linear regression");
  }

  size_t n = x.size();

  // Compute means
  double mean_x = 0.0;
  double mean_y = 0.0;
  for (size_t i = 0; i < n; ++i) {
    mean_x += x[i];
    mean_y += y[i];
  }
  mean_x /= n;
  mean_y /= n;

  // Compute slope and intercept
  double numerator = 0.0;
  double denominator = 0.0;
  double ss_tot = 0.0;

  for (size_t i = 0; i < n; ++i) {
    double dx = x[i] - mean_x;
    double dy = y[i] - mean_y;
    numerator += dx * dy;
    denominator += dx * dx;
    ss_tot += dy * dy;
  }

  if (denominator < 1e-15) {
    throw std::runtime_error("Degenerate data: all x values are identical");
  }

  double slope = numerator / denominator;
  double intercept = mean_y - slope * mean_x;

  // Compute R^2
  double ss_res = 0.0;
  for (size_t i = 0; i < n; ++i) {
    double y_pred = slope * x[i] + intercept;
    double residual = y[i] - y_pred;
    ss_res += residual * residual;
  }

  double r_squared = (ss_tot > 1e-15) ? (1.0 - ss_res / ss_tot) : 0.0;

  return {slope, intercept, r_squared};
}
}  // anonymous namespace

auto fit_exponential(const std::vector<double>& x,
                     const std::vector<double>& y) -> ExponentialFitResult {
  ExponentialFitResult result{0.0, 0.0, 0.0, false};

  if (x.size() != y.size() || x.size() < 2) {
    return result;  // Invalid input
  }

  // Filter out non-positive y values and create log-transformed data
  std::vector<double> x_valid;
  std::vector<double> log_y_valid;

  for (size_t i = 0; i < y.size(); ++i) {
    if (y[i] > 0.0 && std::isfinite(y[i]) && std::isfinite(x[i])) {
      x_valid.push_back(x[i]);
      log_y_valid.push_back(std::log(y[i]));
    }
  }

  if (x_valid.size() < 2) {
    return result;  // Not enough valid data points
  }

  try {
    // Perform linear regression on log-transformed y
    // log(y) = log(a) + b*x
    auto [slope, intercept, r_squared] =
        linear_regression(x_valid, log_y_valid);

    result.b = slope;
    result.a = std::exp(intercept);
    result.r_squared = r_squared;
    result.valid = true;

  } catch (const std::exception&) {
    result.valid = false;
  }

  return result;
}

auto fit_power_law(const std::vector<double>& x,
                   const std::vector<double>& y) -> PowerLawFitResult {
  PowerLawFitResult result{0.0, 0.0, 0.0, false};

  if (x.size() != y.size() || x.size() < 2) {
    return result;  // Invalid input
  }

  // Filter out non-positive values and create log-transformed data
  std::vector<double> log_x_valid;
  std::vector<double> log_y_valid;

  for (size_t i = 0; i < x.size(); ++i) {
    if (x[i] > 0.0 && y[i] > 0.0 && std::isfinite(x[i]) &&
        std::isfinite(y[i])) {
      log_x_valid.push_back(std::log(x[i]));
      log_y_valid.push_back(std::log(y[i]));
    }
  }

  if (log_x_valid.size() < 2) {
    return result;  // Not enough valid data points
  }

  try {
    // Perform linear regression on log-log transformed data
    // log(y) = log(a) + alpha*log(x)
    auto [slope, intercept, r_squared] =
        linear_regression(log_x_valid, log_y_valid);

    result.alpha = slope;
    result.a = std::exp(intercept);
    result.r_squared = r_squared;
    result.valid = true;

  } catch (const std::exception&) {
    result.valid = false;
  }

  return result;
}

auto choose_best_fit(const std::vector<double>& x,
                     const std::vector<double>& y) -> FitType {
  auto exp_result = fit_exponential(x, y);
  auto pow_result = fit_power_law(x, y);

  // If neither is valid, return NONE
  if (!exp_result.valid && !pow_result.valid) {
    return FitType::NONE;
  }

  // If only one is valid, return that one
  if (!exp_result.valid) {
    return FitType::POWER_LAW;
  }
  if (!pow_result.valid) {
    return FitType::EXPONENTIAL;
  }

  // Both are valid, compare R^2 values
  // Higher R^2 means better fit
  return (exp_result.r_squared >= pow_result.r_squared) ? FitType::EXPONENTIAL
                                                        : FitType::POWER_LAW;
}

}  // namespace fitters