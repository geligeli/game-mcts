#ifndef GAME_MCTS_GAME_MCTS_CPP_MCTS_STARTS_AND_BARS_H
#define GAME_MCTS_GAME_MCTS_CPP_MCTS_STARTS_AND_BARS_H
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

    Iterator(int N, int K);

    auto operator*() const -> reference;
    auto operator->() const -> pointer;

    // Prefix increment
    auto operator++() -> Iterator&;

    // Postfix increment
    auto operator++(int) -> Iterator;

    friend auto operator==(const Iterator& a, const Iterator& b) -> bool;
    friend auto operator!=(const Iterator& a, const Iterator& b) -> bool;

   private:
    std::vector<int> sequence_;
    std::vector<int> urns_;
    int K_;
    bool is_end_;

    // Translates the 0s and 1s back into bin counts.
    // Runs in O(N + K) time.
    void update_urns();
  };

  StarsAndBars(int N, int K);

  auto begin() const -> Iterator;
  auto end() const -> Iterator;

 private:
  int N_;
  int K_;
};

}  // namespace mcts

#endif  // GAME_MCTS_GAME_MCTS_CPP_MCTS_STARTS_AND_BARS_H
