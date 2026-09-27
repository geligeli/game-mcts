#ifndef GAME_MCTS_GAME_MCTS_CORE_UTIL_STARTS_AND_BARS_H
#define GAME_MCTS_GAME_MCTS_CORE_UTIL_STARTS_AND_BARS_H
#include <cstddef>
#include <iterator>
#include <vector>

namespace mcts {
class StarsAndBars {
 public:
  class Iterator {
   public:
    // Standard LegacyForwardIterator aliases
    using iterator_category = std::forward_iterator_tag;
    using value_type = std::vector<int>;
    using difference_type = std::ptrdiff_t;
    using pointer = const std::vector<int>*;
    using reference = const std::vector<int>&;

    // Default constructor creates an end-of-sequence iterator
    Iterator();

    Iterator(int n, int k);

    reference operator*() const;
    pointer operator->() const;

    // Prefix increment
    Iterator& operator++();

    // Postfix increment
    Iterator operator++(int);

    friend bool operator==(const Iterator& a, const Iterator& b);
    friend bool operator!=(const Iterator& a, const Iterator& b);

   private:
    std::vector<int> sequence_;
    std::vector<int> urns_;
    int k_;
    bool is_end_;

    // Translates the 0s and 1s back into bin counts.
    // Runs in O(N + K) time.
    void UpdateUrns();
  };

  StarsAndBars(int n, int k);

  Iterator begin() const;
  Iterator end() const;

 private:
  int n_;
  int k_;
};

}  // namespace mcts

#endif  // GAME_MCTS_GAME_MCTS_CORE_UTIL_STARTS_AND_BARS_H
