#include "game_mcts/core/util/starts_and_bars.h"

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <stdexcept>
#include <vector>

namespace mcts {

// Default constructor creates an end-of-sequence iterator
StarsAndBars::Iterator::Iterator() : k_(0), is_end_(true) {}

StarsAndBars::Iterator::Iterator(int n, int k) : k_(k), is_end_(false) {
  if (k_ <= 0) {
    is_end_ = true;
    return;
  }
  // Initialize sequence: N zeros (balls), K-1 ones (bars)
  sequence_.resize(n + k_ - 1, 0);
  std::fill(sequence_.begin() + n, sequence_.end(), 1);

  UpdateUrns();
}

StarsAndBars::Iterator::reference StarsAndBars::Iterator::operator*() const {
  return urns_;
}
StarsAndBars::Iterator::pointer StarsAndBars::Iterator::operator->() const {
  return &urns_;
}

// Prefix increment
StarsAndBars::Iterator& StarsAndBars::Iterator::operator++() {
  if (is_end_) return *this;

  if (std::next_permutation(sequence_.begin(), sequence_.end())) {
    UpdateUrns();
  } else {
    is_end_ = true;
  }
  return *this;
}

// Postfix increment
StarsAndBars::Iterator StarsAndBars::Iterator::operator++(int) {
  Iterator tmp = *this;
  ++(*this);
  return tmp;
}

// Translates the 0s and 1s back into bin counts.
// Runs in O(N + K) time.
void StarsAndBars::Iterator::UpdateUrns() {
  urns_.assign(k_, 0);
  int current_urn = 0;
  for (int item : sequence_) {
    if (item == 1) {
      current_urn++;
    } else {
      urns_[current_urn]++;
    }
  }
}

bool operator==(const StarsAndBars::Iterator& a,
                const StarsAndBars::Iterator& b) {
  if (a.is_end_ != b.is_end_) return false;
  if (a.is_end_) return true;  // Both are end iterators
  return a.sequence_ == b.sequence_;
}

bool operator!=(const StarsAndBars::Iterator& a,
                const StarsAndBars::Iterator& b) {
  return !(a == b);
}

StarsAndBars::StarsAndBars(int n, int k) : n_(n), k_(k) {
  if (n < 0 || k < 0) {
    throw std::invalid_argument("N and K must be non-negative.");
  }
}

StarsAndBars::Iterator StarsAndBars::begin() const { return Iterator(n_, k_); }
StarsAndBars::Iterator StarsAndBars::end() const { return Iterator(); }

}  // namespace mcts