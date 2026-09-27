#include "game_mcts/core/util/state_space.h"

namespace mcts {

auto Uint64StateSpace::size() const -> cpp_int {
  return boost::multiprecision::cpp_int(max_value_) - min_value_ + 1;
}

auto Uint64StateSpace::sample(std::mt19937& gen) const -> value_type {
  thread_local std::uniform_int_distribution<uint64_t> distribution(min_value_,
                                                                    max_value_);
  return distribution(gen);
};

namespace {
cpp_int exact_binomial(uint64_t n, uint64_t k) {
  if (k > n) return 0;
  if (k == 0 || k == n) return 1;

  // Take advantage of symmetry: nCk == nC(n-k)
  if (k > n / 2) {
    k = n - k;
  }

  cpp_int result = 1;
  for (uint64_t i = 1; i <= k; ++i) {
    result *= (n - i + 1);
    result /= i;
  }

  return result;
}
}  // namespace

auto PlaceNElementsIntoKBinsStateSpace::size() const -> cpp_int {
  return exact_binomial(n_ + k_ - 1, k_ - 1);
}

StarsAndBars PlaceNElementsIntoKBinsStateSpace::All() const {
  return StarsAndBars(n_, k_);
}

std::vector<int> PlaceNElementsIntoKBinsStateSpace::unrank(cpp_int rank) const {
  const int num_slots = n_ + k_ - 1;
  const int num_bars = k_ - 1;

  // Lexicographic unranking of the sorted bar positions: the bars are a
  // combination of num_bars distinct positions out of [1, num_slots].
  std::vector<int> bars;
  bars.reserve(num_bars);
  int start = 1;
  for (int i = 0; i < num_bars; ++i) {
    const int remaining = num_bars - i - 1;
    for (int p = start; p <= num_slots - remaining; ++p) {
      cpp_int count = exact_binomial(num_slots - p, remaining);
      if (rank < count) {
        bars.push_back(p);
        start = p + 1;
        break;
      }
      rank -= count;
    }
  }

  // Translate bar positions into bin counts, same as sample_stars_and_bars.
  std::vector<int> out(k_);
  auto insert_it = out.begin();
  int prev_bar = 0;
  for (int bar : bars) {
    *insert_it++ = bar - prev_bar - 1;
    prev_bar = bar;
  }
  *insert_it = (num_slots + 1) - prev_bar - 1;
  return out;
}

}  // namespace mcts