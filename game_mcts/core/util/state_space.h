#ifndef GAME_MCTS_GAME_MCTS_CORE_UTIL_STATE_SPACE_H
#define GAME_MCTS_GAME_MCTS_CORE_UTIL_STATE_SPACE_H

#include <algorithm>
#include <boost/iterator/counting_iterator.hpp>
#include <boost/multiprecision/cpp_int.hpp>
#include <boost/random/discrete_distribution.hpp>
#include <cmath>
#include <concepts>
#include <cstdint>
#include <optional>
#include <random>
#include <unordered_map>
#include <utility>
#include <variant>

#include "game_mcts/core/util/starts_and_bars.h"

namespace mcts {

using namespace boost::multiprecision;

template <std::integral T, T MAX_VALUE, T MIN_VALUE = 0>
struct DiscreteStateSpace {
  using value_type = T;
  cpp_int size() const {
    return boost::multiprecision::cpp_int(MAX_VALUE) - MIN_VALUE + 1;
  }

  value_type sample(std::mt19937& gen) const {
    thread_local std::uniform_int_distribution<T> distribution(MIN_VALUE,
                                                               MAX_VALUE);
    return distribution(gen);
  };
};

struct Uint64StateSpace {
  uint64_t max_value_;
  uint64_t min_value_;
  using value_type = uint64_t;

  cpp_int size() const;
  value_type sample(std::mt19937& gen) const;
};

template <typename A, typename B>
struct ConcatenatedStateSpace {
  using value_type =
      std::variant<typename A::value_type, typename B::value_type>;
  [[no_unique_address]] A a_;
  [[no_unique_address]] B b_;

  cpp_int size() const { return a_.size() + b_.size(); }

  value_type sample(std::mt19937& gen) const {
    thread_local boost::random::uniform_int_distribution<cpp_int> distrib(
        0, a_.size() + b_.size() - 1);
    cpp_int index = distrib(gen);
    if (index < a_.size()) {
      return value_type{std::in_place_index_t<0>{}, a_.sample(gen)};
    } else {
      return value_type{std::in_place_index_t<1>{}, b_.sample(gen)};
    }
  }
};

template <typename A, typename B>
struct ProductStateSpace {
  using value_type = std::pair<typename A::value_type, typename B::value_type>;
  [[no_unique_address]] A a_;
  [[no_unique_address]] B b_;

  cpp_int size() const { return a_.size() * b_.size(); }

  value_type sample(std::mt19937& gen) const {
    return {a_.sample(gen), b_.sample(gen)};
  }
};

struct PlaceNElementsIntoKBinsStateSpace {
  int n_;
  int k_;

  cpp_int size() const;
  template <typename T>
  void sample(std::mt19937& gen, std::vector<T>& out) const;
  StarsAndBars All() const;

  // Bijection [0, size()) -> placements, in lexicographic order of the sorted
  // bar positions. Enables sampling without replacement via IndexActionSampler
  // without materializing the space.
  std::vector<int> unrank(cpp_int rank) const;
};

// Draws uniformly from [0, space_size) without replacement using a partial
// Fisher-Yates shuffle over a sparse map. O(1) per draw, O(k) memory after
// k draws, no collisions. Exhausts exactly after space_size draws.
struct IndexActionSampler {
  uint64_t space_size_;
  uint64_t num_drawn_ = 0;
  std::unordered_map<uint64_t, uint64_t> swapped_{};

  uint64_t at(uint64_t index) const {
    auto it = swapped_.find(index);
    return it == swapped_.end() ? index : it->second;
  }

  std::optional<uint64_t> next(std::mt19937& gen) {
    if (num_drawn_ >= space_size_) {
      return std::nullopt;
    }
    std::uniform_int_distribution<uint64_t> distribution(num_drawn_,
                                                         space_size_ - 1);
    uint64_t j = distribution(gen);
    uint64_t value = at(j);
    swapped_[j] = at(num_drawn_);
    ++num_drawn_;
    return value;
  }
};

// Adapter for huge but rankable spaces: pairs a state space providing an
// integer bijection (size() + unrank(index)) with an IndexActionSampler to
// draw actions without replacement without materializing the space.
// Requires the space size to fit in a uint64_t.
template <typename SPACE, typename ACTION>
  requires requires(SPACE s, uint64_t index) {
    { s.size() } -> std::convertible_to<cpp_int>;
    { s.unrank(index) } -> std::same_as<ACTION>;
  }
struct RankedActionSet {
  using action_t = ACTION;
  SPACE space_;
  IndexActionSampler sampler;

  explicit RankedActionSet(SPACE space)
      : space_(std::move(space)),
        sampler{.space_size_ = static_cast<uint64_t>(this->space_.size())} {}

  std::optional<ACTION> next(std::mt19937& gen) {
    auto index = sampler.next(gen);
    if (!index.has_value()) {
      return std::nullopt;
    }
    return space_.unrank(*index);
  }
};

template <typename T>
void sample_stars_and_bars(int n, int k, std::mt19937& rng,
                           std::vector<T>& out) {
  if (k <= 0) {
    out.clear();
    return;
  }

  if (k == 1) {
    out = {n};
    return;
  }

  int num_slots = n + k - 1;
  int num_bars = k - 1;

  std::vector<T> bars;
  bars.reserve(num_bars);

  // Sample K-1 unique elements without replacement directly from a virtual
  // range. Time complexity depends on the std::sample implementation (typically
  // O(num_slots) or O(num_bars * log(num_bars))), but Space complexity is
  // strictly O(K).
  std::sample(boost::make_counting_iterator(1),
              boost::make_counting_iterator(num_slots + 1),
              std::back_inserter(bars), num_bars, rng);

  // std::sample guarantees the output is sorted if the input iterator is at
  // least a ForwardIterator. boost::counting_iterator is a
  // RandomAccessIterator, so explicit sorting is unnecessary.

  // std::vector<int> urns;
  out.resize(k);
  auto insert_it = out.begin();

  T prev_bar = 0;
  for (T bar : bars) {
    *insert_it++ = bar - prev_bar - 1;
    prev_bar = bar;
  }
  *insert_it = (num_slots + 1) - prev_bar - 1;
}

template <typename T>
void PlaceNElementsIntoKBinsStateSpace::sample(std::mt19937& gen,
                                               std::vector<T>& out) const {
  sample_stars_and_bars(n_, k_, gen, out);
}

}  // namespace mcts

#endif  // GAME_MCTS_GAME_MCTS_CORE_UTIL_STATE_SPACE_H
