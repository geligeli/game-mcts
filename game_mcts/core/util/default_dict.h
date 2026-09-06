#ifndef GAME_MCTS_GAME_MCTS_CPP_MCTS_DEFAULT_DICT_H
#define GAME_MCTS_GAME_MCTS_CPP_MCTS_DEFAULT_DICT_H
#include <unordered_map>

namespace mcts {

template <typename T, typename K, typename V>
concept DefaultDict = requires(T &t, K key) {
  { t[key] } -> std::convertible_to<V &>;
};

template <typename K, typename V>
class MapDefaultDict {
 public:
  auto operator[](const K &key) -> V & { return dict_[key]; }

 private:
  std::unordered_map<K, V> dict_;
};

}  // namespace mcts

#endif  // GAME_MCTS_GAME_MCTS_CPP_MCTS_DEFAULT_DICT_H
