#include "game_mcts/cpp/mcts/starts_and_bars.h"

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <stdexcept>
#include <vector>

namespace mcts {

// Default constructor creates an end-of-sequence iterator
StarsAndBars::Iterator::Iterator() : K_(0), is_end_(true) {}

StarsAndBars::Iterator::Iterator(int N, int K) : K_(K), is_end_(false) {
  if (K_ <= 0) {
    is_end_ = true;
    return;
  }
  // Initialize sequence: N zeros (balls), K-1 ones (bars)
  sequence_.resize(N + K_ - 1, 0);
  std::fill(sequence_.begin() + N, sequence_.end(), 1);

  update_urns();
}

auto StarsAndBars::Iterator::operator*() const
    -> StarsAndBars::Iterator::reference {
  return urns_;
}
auto StarsAndBars::Iterator::operator->() const
    -> StarsAndBars::Iterator::pointer {
  return &urns_;
}

// Prefix increment
auto StarsAndBars::Iterator::operator++() -> StarsAndBars::Iterator& {
  if (is_end_) return *this;

  if (std::next_permutation(sequence_.begin(), sequence_.end())) {
    update_urns();
  } else {
    is_end_ = true;
  }
  return *this;
}

// Postfix increment
auto StarsAndBars::Iterator::operator++(int) -> StarsAndBars::Iterator {
  Iterator tmp = *this;
  ++(*this);
  return tmp;
}

// Translates the 0s and 1s back into bin counts.
// Runs in O(N + K) time.
void StarsAndBars::Iterator::update_urns() {
  urns_.assign(K_, 0);
  int current_urn = 0;
  for (int item : sequence_) {
    if (item == 1) {
      current_urn++;
    } else {
      urns_[current_urn]++;
    }
  }
}

auto operator==(const StarsAndBars::Iterator& a,
                const StarsAndBars::Iterator& b) -> bool {
  if (a.is_end_ != b.is_end_) return false;
  if (a.is_end_) return true;  // Both are end iterators
  return a.sequence_ == b.sequence_;
}

auto operator!=(const StarsAndBars::Iterator& a,
                const StarsAndBars::Iterator& b) -> bool {
  return !(a == b);
}

StarsAndBars::StarsAndBars(int N, int K) : N_(N), K_(K) {
  if (N < 0 || K < 0) {
    throw std::invalid_argument("N and K must be non-negative.");
  }
}

auto StarsAndBars::begin() const -> StarsAndBars::Iterator {
  return Iterator(N_, K_);
}
auto StarsAndBars::end() const -> StarsAndBars::Iterator { return Iterator(); }

}  // namespace mcts