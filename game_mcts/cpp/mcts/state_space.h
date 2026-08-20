#ifndef GAME_MCTS_GAME_MCTS_CPP_MCTS_STATE_SPACE_H
#define GAME_MCTS_GAME_MCTS_CPP_MCTS_STATE_SPACE_H

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

#include "game_mcts/cpp/mcts/starts_and_bars.h"

namespace mcts {

using namespace boost::multiprecision;

template <std::integral T, T MAX_VALUE, T MIN_VALUE = 0>
struct DiscreteStateSpace {
  using value_type = T;
  auto size() const -> cpp_int {
    return boost::multiprecision::cpp_int(MAX_VALUE) - MIN_VALUE + 1;
  }

  auto sample(std::mt19937& gen) const -> value_type {
    thread_local std::uniform_int_distribution<T> distribution(MIN_VALUE,
                                                               MAX_VALUE);
    return distribution(gen);
  };
};

struct Uint64StateSpace {
  uint64_t max_value;
  uint64_t min_value;
  using value_type = uint64_t;

  auto size() const -> cpp_int;
  auto sample(std::mt19937& gen) const -> value_type;
};

template <typename A, typename B>
struct ConcatenatedStateSpace {
  using value_type =
      std::variant<typename A::value_type, typename B::value_type>;
  [[no_unique_address]] A a;
  [[no_unique_address]] B b;

  auto size() const -> cpp_int { return a.size() + b.size(); }

  auto sample(std::mt19937& gen) const -> value_type {
    thread_local boost::random::uniform_int_distribution<cpp_int> distrib(
        0, a.size() + b.size() - 1);
    cpp_int index = distrib(gen);
    if (index < a.size()) {
      return value_type{std::in_place_index_t<0>{}, a.sample(gen)};
    } else {
      return value_type{std::in_place_index_t<1>{}, b.sample(gen)};
    }
  }
};

template <typename A, typename B>
struct ProductStateSpace {
  using value_type = std::pair<typename A::value_type, typename B::value_type>;
  [[no_unique_address]] A a;
  [[no_unique_address]] B b;

  auto size() const -> cpp_int { return a.size() * b.size(); }

  auto sample(std::mt19937& gen) const -> value_type {
    return {a.sample(gen), b.sample(gen)};
  }
};

struct PlaceNElementsIntoKBinsStateSpace {
  int N;
  int K;

  auto size() const -> cpp_int;
  template <typename T>
  void sample(std::mt19937& gen, std::vector<T>& out) const;
  auto all() const -> StarsAndBars;

  // Bijection [0, size()) -> placements, in lexicographic order of the sorted
  // bar positions. Enables sampling without replacement via IndexActionSampler
  // without materializing the space.
  auto unrank(cpp_int rank) const -> std::vector<int>;
};

// Draws uniformly from [0, space_size) without replacement using a partial
// Fisher-Yates shuffle over a sparse map. O(1) per draw, O(k) memory after
// k draws, no collisions. Exhausts exactly after space_size draws.
struct IndexActionSampler {
  uint64_t space_size;
  uint64_t num_drawn = 0;
  std::unordered_map<uint64_t, uint64_t> swapped{};

  auto at(uint64_t index) const -> uint64_t {
    auto it = swapped.find(index);
    return it == swapped.end() ? index : it->second;
  }

  auto next(std::mt19937& gen) -> std::optional<uint64_t> {
    if (num_drawn >= space_size) {
      return std::nullopt;
    }
    std::uniform_int_distribution<uint64_t> distribution(num_drawn,
                                                         space_size - 1);
    uint64_t j = distribution(gen);
    uint64_t value = at(j);
    swapped[j] = at(num_drawn);
    ++num_drawn;
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
  SPACE space;
  IndexActionSampler sampler;

  explicit RankedActionSet(SPACE space)
      : space(std::move(space)),
        sampler{.space_size = static_cast<uint64_t>(this->space.size())} {}

  auto next(std::mt19937& gen) -> std::optional<ACTION> {
    auto index = sampler.next(gen);
    if (!index.has_value()) {
      return std::nullopt;
    }
    return space.unrank(*index);
  }
};

template <typename T>
void sample_stars_and_bars(int N, int K, std::mt19937& rng,
                           std::vector<T>& out) {
  if (K <= 0) {
    out.clear();
    return;
  }

  if (K == 1) {
    out = {N};
    return;
  }

  int num_slots = N + K - 1;
  int num_bars = K - 1;

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
  out.resize(K);
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
  sample_stars_and_bars(N, K, gen, out);
}

}  // namespace mcts

#endif  // GAME_MCTS_GAME_MCTS_CPP_MCTS_STATE_SPACE_H
