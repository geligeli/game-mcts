#include "game_mcts/cpp/fitters/simple_fitters.h"

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

namespace fitters {
namespace {

// Helper to generate test data
auto linspace(double start, double end, size_t n) -> std::vector<double> {
  std::vector<double> result(n);
  for (size_t i = 0; i < n; ++i) {
    result[i] = start + (end - start) * i / (n - 1);
  }
  return result;
}

TEST(ExponentialFitTest, FitsPerfectExponential) {
  // Generate data for y = 2.0 * exp(0.5 * x)
  auto x = linspace(0.0, 10.0, 50);
  std::vector<double> y(x.size());

  double a_true = 2.0;
  double b_true = 0.5;

  for (size_t i = 0; i < x.size(); ++i) {
    y[i] = a_true * std::exp(b_true * x[i]);
  }

  auto result = fit_exponential(x, y);

  EXPECT_TRUE(result.valid);
  EXPECT_NEAR(result.a, a_true, 1e-6);
  EXPECT_NEAR(result.b, b_true, 1e-6);
  EXPECT_NEAR(result.r_squared, 1.0, 1e-10);
}

TEST(ExponentialFitTest, FitsDecayingExponential) {
  // Generate data for y = 100.0 * exp(-0.3 * x)
  auto x = linspace(0.0, 10.0, 50);
  std::vector<double> y(x.size());

  double a_true = 100.0;
  double b_true = -0.3;

  for (size_t i = 0; i < x.size(); ++i) {
    y[i] = a_true * std::exp(b_true * x[i]);
  }

  auto result = fit_exponential(x, y);

  EXPECT_TRUE(result.valid);
  EXPECT_NEAR(result.a, a_true, 1e-6);
  EXPECT_NEAR(result.b, b_true, 1e-6);
  EXPECT_NEAR(result.r_squared, 1.0, 1e-10);
}

TEST(ExponentialFitTest, HandlesNegativeValues) {
  // Mix of positive and negative values
  std::vector<double> x = {1.0, 2.0, 3.0, 4.0, 5.0};
  std::vector<double> y = {2.0, -1.0, 8.0, 16.0, 32.0};

  auto result = fit_exponential(x, y);

  // Should still fit the positive values
  EXPECT_TRUE(result.valid);
  // Should have filtered out negative values
}

TEST(ExponentialFitTest, HandlesInsufficientData) {
  std::vector<double> x = {1.0};
  std::vector<double> y = {2.0};

  auto result = fit_exponential(x, y);

  EXPECT_FALSE(result.valid);
}

TEST(ExponentialFitTest, HandlesAllNegativeY) {
  std::vector<double> x = {1.0, 2.0, 3.0, 4.0};
  std::vector<double> y = {-1.0, -2.0, -3.0, -4.0};

  auto result = fit_exponential(x, y);

  EXPECT_FALSE(result.valid);
}

TEST(PowerLawFitTest, FitsPerfectPowerLaw) {
  // Generate data for y = 3.0 * x^2.5
  auto x = linspace(1.0, 10.0, 50);
  std::vector<double> y(x.size());

  double a_true = 3.0;
  double alpha_true = 2.5;

  for (size_t i = 0; i < x.size(); ++i) {
    y[i] = a_true * std::pow(x[i], alpha_true);
  }

  auto result = fit_power_law(x, y);

  EXPECT_TRUE(result.valid);
  EXPECT_NEAR(result.a, a_true, 1e-6);
  EXPECT_NEAR(result.alpha, alpha_true, 1e-6);
  EXPECT_NEAR(result.r_squared, 1.0, 1e-10);
}

TEST(PowerLawFitTest, FitsDecayingPowerLaw) {
  // Generate data for y = 100.0 * x^(-0.8) (typical convergence pattern)
  auto x = linspace(1.0, 100.0, 50);
  std::vector<double> y(x.size());

  double a_true = 100.0;
  double alpha_true = -0.8;

  for (size_t i = 0; i < x.size(); ++i) {
    y[i] = a_true * std::pow(x[i], alpha_true);
  }

  auto result = fit_power_law(x, y);

  EXPECT_TRUE(result.valid);
  EXPECT_NEAR(result.a, a_true, 1e-6);
  EXPECT_NEAR(result.alpha, alpha_true, 1e-6);
  EXPECT_NEAR(result.r_squared, 1.0, 1e-10);
}

TEST(PowerLawFitTest, HandlesNonPositiveX) {
  // Mix of positive and non-positive x values
  std::vector<double> x = {-1.0, 0.0, 1.0, 2.0, 3.0};
  std::vector<double> y = {1.0, 2.0, 3.0, 6.0, 11.0};

  auto result = fit_power_law(x, y);

  // Should still fit the positive x values
  EXPECT_TRUE(result.valid);
}

TEST(PowerLawFitTest, HandlesInsufficientData) {
  std::vector<double> x = {1.0};
  std::vector<double> y = {2.0};

  auto result = fit_power_law(x, y);

  EXPECT_FALSE(result.valid);
}

TEST(PowerLawFitTest, HandlesAllNonPositive) {
  std::vector<double> x = {-1.0, -2.0, -3.0, -4.0};
  std::vector<double> y = {1.0, 2.0, 3.0, 4.0};

  auto result = fit_power_law(x, y);

  EXPECT_FALSE(result.valid);
}

TEST(ChooseBestFitTest, ChoosesExponentialForExponentialData) {
  // Generate exponential data
  auto x = linspace(0.0, 10.0, 50);
  std::vector<double> y(x.size());

  for (size_t i = 0; i < x.size(); ++i) {
    y[i] = 5.0 * std::exp(0.3 * x[i]);
  }

  auto fit_type = choose_best_fit(x, y);

  EXPECT_EQ(fit_type, FitType::EXPONENTIAL);
}

TEST(ChooseBestFitTest, ChoosesPowerLawForPowerLawData) {
  // Generate power law data
  auto x = linspace(1.0, 100.0, 50);
  std::vector<double> y(x.size());

  for (size_t i = 0; i < x.size(); ++i) {
    y[i] = 10.0 * std::pow(x[i], -0.5);
  }

  auto fit_type = choose_best_fit(x, y);

  EXPECT_EQ(fit_type, FitType::POWER_LAW);
}

TEST(ChooseBestFitTest, ReturnsNoneForInvalidData) {
  std::vector<double> x = {1.0, 2.0};
  std::vector<double> y = {-1.0, -2.0};

  auto fit_type = choose_best_fit(x, y);

  EXPECT_EQ(fit_type, FitType::NONE);
}

TEST(ChooseBestFitTest, ConvergenceScenario) {
  // Typical convergence scenario: error decreasing over iterations
  // Using power law: error = 1.0 / iteration^0.5
  std::vector<double> iterations;
  std::vector<double> errors;

  for (int i = 1; i <= 100; ++i) {
    iterations.push_back(static_cast<double>(i));
    errors.push_back(1.0 / std::sqrt(static_cast<double>(i)));
  }

  auto fit_type = choose_best_fit(iterations, errors);

  // Power law should fit better for this convergence pattern
  EXPECT_EQ(fit_type, FitType::POWER_LAW);
}

TEST(ChooseBestFitTest, ExponentialConvergenceScenario) {
  // Exponential convergence: error decreasing exponentially
  std::vector<double> iterations;
  std::vector<double> errors;

  for (int i = 0; i < 50; ++i) {
    iterations.push_back(static_cast<double>(i));
    errors.push_back(10.0 * std::exp(-0.1 * i));
  }

  auto fit_type = choose_best_fit(iterations, errors);

  // Exponential should fit better for this convergence pattern
  EXPECT_EQ(fit_type, FitType::EXPONENTIAL);
}

}  // namespace
}  // namespace fitters
