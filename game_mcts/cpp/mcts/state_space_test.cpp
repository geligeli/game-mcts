#include "game_mcts/cpp/mcts/state_space.h"

#include <gtest/gtest.h>

#include <numeric>
#include <set>
#include <variant>

namespace mcts {

TEST(StateSpaceTest, DiscreteStateSpaceDefaultMin) {
  DiscreteStateSpace<int, 10> space;
  EXPECT_EQ(space.size(), 11);
}

TEST(StateSpaceTest, DiscreteStateSpaceCustomMin) {
  DiscreteStateSpace<int, 10, 5> space;
  EXPECT_EQ(space.size(), 6);
}

TEST(StateSpaceTest, DiscreteStateSpaceSingleValue) {
  DiscreteStateSpace<int, 3, 3> space;
  EXPECT_EQ(space.size(), 1);
}

TEST(StateSpaceTest, DiscreteStateSpaceSample) {
  DiscreteStateSpace<int, 5, 2> space;
  std::mt19937 gen(42);
  for (int i = 0; i < 100; ++i) {
    int val = space.sample(gen);
    EXPECT_GE(val, 2);
    EXPECT_LE(val, 5);
  }
}

TEST(StateSpaceTest, DiscreteStateSpaceSampleCoverage) {
  DiscreteStateSpace<int, 4, 1> space;
  std::mt19937 gen(123);
  std::set<int> seen;
  for (int i = 0; i < 1000; ++i) {
    seen.insert(space.sample(gen));
  }
  EXPECT_EQ(seen.size(), 4);
  EXPECT_EQ(*seen.begin(), 1);
  EXPECT_EQ(*seen.rbegin(), 4);
}

TEST(StateSpaceTest, DiscreteStateSpaceZeroMin) {
  DiscreteStateSpace<int, 0> space;
  EXPECT_EQ(space.size(), 1);
}

TEST(StateSpaceTest, ConcatenatedStateSpaceSize) {
  ConcatenatedStateSpace<DiscreteStateSpace<int, 3>, DiscreteStateSpace<int, 4>>
      space;
  // sizes: 4 + 5 = 9
  EXPECT_EQ(space.size(), 9);
}

TEST(StateSpaceTest, ConcatenatedStateSpaceSample) {
  ConcatenatedStateSpace<DiscreteStateSpace<int, 3>, DiscreteStateSpace<int, 4>>
      space;
  std::mt19937 gen(42);
  for (int i = 0; i < 100; ++i) {
    auto val = space.sample(gen);
    int as_int = std::visit([](int v) { return v; }, val);
    EXPECT_GE(as_int, 0);
    EXPECT_LE(as_int, 4);
  }
}

TEST(StateSpaceTest, ConcatenatedStateSpaceSampleBothBranches) {
  ConcatenatedStateSpace<DiscreteStateSpace<int, 0>,
                         DiscreteStateSpace<int, 100, 100>>
      space;
  std::mt19937 gen(42);
  bool saw_left = false, saw_right = false;
  for (int i = 0; i < 200; ++i) {
    auto val = space.sample(gen);
    if (val.index() == 0) {
      EXPECT_EQ(std::get<0>(val), 0);
      saw_left = true;
    } else {
      EXPECT_EQ(std::get<1>(val), 100);
      saw_right = true;
    }
  }
  EXPECT_TRUE(saw_left);
  EXPECT_TRUE(saw_right);
}

TEST(StateSpaceTest, ProductStateSpaceSize) {
  ProductStateSpace<DiscreteStateSpace<int, 3>, DiscreteStateSpace<int, 4>>
      space;
  // sizes: 4 * 5 = 20
  EXPECT_EQ(space.size(), 20);
}

TEST(StateSpaceTest, ProductStateSpaceSample) {
  ProductStateSpace<DiscreteStateSpace<int, 3>, DiscreteStateSpace<int, 4>>
      space;
  std::mt19937 gen(42);
  for (int i = 0; i < 100; ++i) {
    auto [a, b] = space.sample(gen);
    EXPECT_GE(a, 0);
    EXPECT_LE(a, 3);
    EXPECT_GE(b, 0);
    EXPECT_LE(b, 4);
  }
}

TEST(StateSpaceTest, ProductStateSpaceSampleCoverage) {
  ProductStateSpace<DiscreteStateSpace<int, 1>, DiscreteStateSpace<int, 1>>
      space;
  std::mt19937 gen(42);
  std::set<std::pair<int, int>> seen;
  for (int i = 0; i < 500; ++i) {
    seen.insert(space.sample(gen));
  }
  // 2 * 2 = 4 combinations
  EXPECT_EQ(seen.size(), 4);
}

TEST(StateSpaceTest, NestedProductStateSpaceSize) {
  ProductStateSpace<
      DiscreteStateSpace<int, 2>,
      ProductStateSpace<DiscreteStateSpace<int, 3>, DiscreteStateSpace<int, 4>>>
      space;
  // sizes: 3 * (4 * 5) = 60
  EXPECT_EQ(space.size(), 60);
}

TEST(StateSpaceTest, NestedConcatenatedStateSpaceSize) {
  ConcatenatedStateSpace<DiscreteStateSpace<int, 2>,
                         ConcatenatedStateSpace<DiscreteStateSpace<int, 3>,
                                                DiscreteStateSpace<int, 4>>>
      space;
  // sizes: 3 + (4 + 5) = 12
  EXPECT_EQ(space.size(), 12);
}

TEST(StateSpaceTest, ProductOfConcatenatedSize) {
  using Left = ConcatenatedStateSpace<DiscreteStateSpace<int, 2>,
                                      DiscreteStateSpace<int, 3>>;
  using Right = DiscreteStateSpace<int, 1>;
  ProductStateSpace<Left, Right> space;
  // (3 + 4) * 2 = 14
  EXPECT_EQ(space.size(), 14);
}

// --- PlaceNElementsIntoKBinsStateSpace tests ---

TEST(PlaceNElementsIntoKBinsTest, SizeBasic) {
  // C(N+K-1, K-1) = C(5+3-1, 3-1) = C(7,2) = 21
  PlaceNElementsIntoKBinsStateSpace space{.N = 5, .K = 3};
  EXPECT_EQ(space.size(), 21);
}

TEST(PlaceNElementsIntoKBinsTest, SizeOneBin) {
  // C(N+0, 0) = 1: only one way to put everything in one bin
  PlaceNElementsIntoKBinsStateSpace space{.N = 10, .K = 1};
  EXPECT_EQ(space.size(), 1);
}

TEST(PlaceNElementsIntoKBinsTest, SizeZeroElements) {
  // C(0+K-1, K-1) = 1: all bins are empty
  PlaceNElementsIntoKBinsStateSpace space{.N = 0, .K = 5};
  EXPECT_EQ(space.size(), 1);
}

TEST(PlaceNElementsIntoKBinsTest, SizeOneElementManyBins) {
  // C(1+K-1, K-1) = C(K, K-1) = K
  PlaceNElementsIntoKBinsStateSpace space{.N = 1, .K = 7};
  EXPECT_EQ(space.size(), 7);
}

TEST(PlaceNElementsIntoKBinsTest, SizeLarge) {
  // C(20+5-1, 5-1) = C(24,4) = 10626
  PlaceNElementsIntoKBinsStateSpace space{.N = 20, .K = 5};
  EXPECT_EQ(space.size(), 10626);
}

TEST(PlaceNElementsIntoKBinsTest, SizeOverflows64Bit) {
  // C(50+51-1, 51-1) = C(100,50) which is ~1.0 * 10^29, well beyond 2^64.
  PlaceNElementsIntoKBinsStateSpace space{.N = 50, .K = 51};
  cpp_int expected("100891344545564193334812497256");
  EXPECT_EQ(space.size(), expected);
}

TEST(PlaceNElementsIntoKBinsTest, SampleReturnsCorrectBinCount) {
  PlaceNElementsIntoKBinsStateSpace space{.N = 10, .K = 4};
  std::mt19937 gen(42);
  for (int i = 0; i < 50; ++i) {
    std::vector<int> result;
    space.sample(gen, result);
    EXPECT_EQ(static_cast<int>(result.size()), 4);
  }
}

TEST(PlaceNElementsIntoKBinsTest, SampleSumsToN) {
  PlaceNElementsIntoKBinsStateSpace space{.N = 15, .K = 6};
  std::mt19937 gen(99);
  for (int i = 0; i < 100; ++i) {
    std::vector<int> result;
    space.sample(gen, result);
    int total = std::accumulate(result.begin(), result.end(), 0);
    EXPECT_EQ(total, 15);
  }
}

TEST(PlaceNElementsIntoKBinsTest, SampleValuesNonNegative) {
  PlaceNElementsIntoKBinsStateSpace space{.N = 8, .K = 5};
  std::mt19937 gen(7);
  for (int i = 0; i < 100; ++i) {
    std::vector<int> result;
    space.sample(gen, result);
    for (int v : result) {
      EXPECT_GE(v, 0);
    }
  }
}

TEST(PlaceNElementsIntoKBinsTest, SampleOneBinReturnsAll) {
  PlaceNElementsIntoKBinsStateSpace space{.N = 12, .K = 1};
  std::mt19937 gen(42);
  std::vector<int> result;
  space.sample(gen, result);
  ASSERT_EQ(result.size(), 1);
  EXPECT_EQ(result[0], 12);
}

TEST(PlaceNElementsIntoKBinsTest, SampleZeroElements) {
  PlaceNElementsIntoKBinsStateSpace space{.N = 0, .K = 3};
  std::mt19937 gen(42);
  std::vector<int> result;
  space.sample(gen, result);
  ASSERT_EQ(result.size(), 3);
  for (int v : result) {
    EXPECT_EQ(v, 0);
  }
}

TEST(PlaceNElementsIntoKBinsTest, UnrankSumsToN) {
  PlaceNElementsIntoKBinsStateSpace space{.N = 15, .K = 6};
  for (cpp_int rank = 0; rank < space.size(); ++rank) {
    std::vector<int> result = space.unrank(rank);
    ASSERT_EQ(result.size(), 6u);
    EXPECT_EQ(std::accumulate(result.begin(), result.end(), 0), 15);
  }
}

TEST(PlaceNElementsIntoKBinsTest, UnrankIsBijective) {
  PlaceNElementsIntoKBinsStateSpace space{.N = 7, .K = 4};
  std::set<std::vector<int>> seen;
  for (cpp_int rank = 0; rank < space.size(); ++rank) {
    seen.insert(space.unrank(rank));
  }
  // Every rank produced a distinct placement covering the whole space.
  EXPECT_EQ(static_cast<cpp_int>(seen.size()), space.size());
}

TEST(PlaceNElementsIntoKBinsTest, UnrankMatchesEnumeration) {
  PlaceNElementsIntoKBinsStateSpace space{.N = 5, .K = 3};
  std::set<std::vector<int>> enumerated;
  for (const auto& placement : space.all()) {
    enumerated.insert(placement);
  }
  for (cpp_int rank = 0; rank < space.size(); ++rank) {
    EXPECT_TRUE(enumerated.contains(space.unrank(rank)));
  }
}

TEST(PlaceNElementsIntoKBinsTest, UnrankOneBin) {
  PlaceNElementsIntoKBinsStateSpace space{.N = 12, .K = 1};
  EXPECT_EQ(space.unrank(0), std::vector<int>{12});
}

// --- IndexActionSampler tests ---

TEST(IndexActionSamplerTest, DrawsWithoutReplacementUntilExhausted) {
  IndexActionSampler sampler{.space_size = 100};
  std::mt19937 gen(42);
  std::set<uint64_t> seen;
  for (int i = 0; i < 100; ++i) {
    auto value = sampler.next(gen);
    ASSERT_TRUE(value.has_value());
    EXPECT_LT(*value, 100u);
    EXPECT_TRUE(seen.insert(*value).second) << "Duplicate draw " << *value;
  }
  EXPECT_EQ(sampler.next(gen), std::nullopt);
}

TEST(IndexActionSamplerTest, CoversWholeSpace) {
  IndexActionSampler sampler{.space_size = 50};
  std::mt19937 gen(7);
  std::set<uint64_t> seen;
  while (auto value = sampler.next(gen)) {
    seen.insert(*value);
  }
  EXPECT_EQ(seen.size(), 50u);
  EXPECT_EQ(*seen.begin(), 0u);
  EXPECT_EQ(*seen.rbegin(), 49u);
}

TEST(IndexActionSamplerTest, EmptySpaceIsImmediatelyExhausted) {
  IndexActionSampler sampler{.space_size = 0};
  std::mt19937 gen(42);
  EXPECT_EQ(sampler.next(gen), std::nullopt);
}

// --- RankedActionSet tests ---

TEST(RankedActionSetTest, DrawsDistinctPlacementsUntilExhausted) {
  RankedActionSet<PlaceNElementsIntoKBinsStateSpace, std::vector<int>>
      action_set(PlaceNElementsIntoKBinsStateSpace{.N = 5, .K = 3});
  std::mt19937 gen(42);
  std::set<std::vector<int>> seen;
  while (auto action = action_set.next(gen)) {
    EXPECT_EQ(std::accumulate(action->begin(), action->end(), 0), 5);
    EXPECT_TRUE(seen.insert(*action).second);
  }
  // C(5+3-1, 3-1) = 21 placements, all drawn exactly once.
  EXPECT_EQ(seen.size(), 21u);
}

}  // namespace mcts