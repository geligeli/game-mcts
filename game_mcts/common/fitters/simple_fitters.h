#ifndef GAME_MCTS_GAME_MCTS_CPP_FITTERS_SIMPLE_FITTERS_H
#define GAME_MCTS_GAME_MCTS_CPP_FITTERS_SIMPLE_FITTERS_H
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <vector>

namespace fitters {

// Result structure for exponential fit: y = a * exp(b * x)
struct ExponentialFitResult {
  double a;          // Scale parameter
  double b;          // Exponential rate
  double r_squared;  // Coefficient of determination
  bool valid;        // Whether the fit was successful
};

// Result structure for power law fit: y = a * x^alpha
struct PowerLawFitResult {
  double a;          // Scale parameter
  double alpha;      // Power exponent
  double r_squared;  // Coefficient of determination
  bool valid;        // Whether the fit was successful
};

// Fits an exponential function y = a * exp(b * x) to the data
// Uses log-linear regression for robustness
// x: independent variable values
// y: dependent variable values (must be positive)
// Returns: ExponentialFitResult with fit parameters
ExponentialFitResult fit_exponential(const std::vector<double>& x,
                                     const std::vector<double>& y);

// Fits a power law function y = a * x^alpha to the data
// Uses log-log regression for robustness
// x: independent variable values (must be positive)
// y: dependent variable values (must be positive)
// Returns: PowerLawFitResult with fit parameters
PowerLawFitResult fit_power_law(const std::vector<double>& x,
                                const std::vector<double>& y);

// Enum for the type of fit
enum class FitType {
  EXPONENTIAL,
  POWER_LAW,
  NONE  // When neither fit is valid
};

// Tries both exponential and power law fits, returns which is more likely
// based on R^2 values. Returns NONE if neither fit is valid.
FitType choose_best_fit(const std::vector<double>& x,
                        const std::vector<double>& y);

}  // namespace fitters

#endif  // GAME_MCTS_GAME_MCTS_CPP_FITTERS_SIMPLE_FITTERS_H
