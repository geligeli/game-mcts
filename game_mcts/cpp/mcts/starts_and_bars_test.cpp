#include "game_mcts/cpp/mcts/starts_and_bars.h"

#include <gtest/gtest.h>

#include <numeric>
#include <set>
#include <vector>

namespace mcts {

// ---- Exact enumeration tests ------------------------------------------------

TEST(StarsAndBars, N0K1) {
  StarsAndBars sb(0, 1);
  std::vector<std::vector<int>> got(sb.begin(), sb.end());
  EXPECT_EQ(got, (std::vector<std::vector<int>>{{0}}));
}

TEST(StarsAndBars, N1K1) {
  StarsAndBars sb(1, 1);
  std::vector<std::vector<int>> got(sb.begin(), sb.end());
  EXPECT_EQ(got, (std::vector<std::vector<int>>{{1}}));
}

TEST(StarsAndBars, N0K2) {
  StarsAndBars sb(0, 2);
  std::vector<std::vector<int>> got(sb.begin(), sb.end());
  EXPECT_EQ(got, (std::vector<std::vector<int>>{{0, 0}}));
}

TEST(StarsAndBars, N2K2ExactOrder) {
  // Sequences (0-indexed bars): 001 -> {2,0}, 010 -> {1,1}, 100 -> {0,2}
  StarsAndBars sb(2, 2);
  std::vector<std::vector<int>> got(sb.begin(), sb.end());
  std::vector<std::vector<int>> expected = {{2, 0}, {1, 1}, {0, 2}};
  EXPECT_EQ(got, expected);
}

TEST(StarsAndBars, N3K2ExactOrder) {
  StarsAndBars sb(3, 2);
  std::vector<std::vector<int>> got(sb.begin(), sb.end());
  std::vector<std::vector<int>> expected = {{3, 0}, {2, 1}, {1, 2}, {0, 3}};
  EXPECT_EQ(got, expected);
}

TEST(StarsAndBars, N2K3ExactOrder) {
  // 6 combinations: C(4,2)=6
  StarsAndBars sb(2, 3);
  std::vector<std::vector<int>> got(sb.begin(), sb.end());
  std::vector<std::vector<int>> expected = {{2, 0, 0}, {1, 1, 0}, {1, 0, 1},
                                            {0, 2, 0}, {0, 1, 1}, {0, 0, 2}};
  EXPECT_EQ(got, expected);
}

// ---- Count tests (C(N+K-1, K-1)) -------------------------------------------

// Returns C(n, r) for small n.
static auto binom(int n, int r) -> int {
  if (r < 0 || r > n) return 0;
  if (r == 0 || r == n) return 1;
  int result = 1;
  for (int i = 0; i < r; ++i) {
    result = result * (n - i) / (i + 1);
  }
  return result;
}

TEST(StarsAndBars, CountMatchesBinomialCoefficient) {
  for (int N = 0; N <= 5; ++N) {
    for (int K = 1; K <= 5; ++K) {
      StarsAndBars sb(N, K);
      int count = std::distance(sb.begin(), sb.end());
      EXPECT_EQ(count, binom(N + K - 1, K - 1)) << "N=" << N << " K=" << K;
    }
  }
}

// ---- Each combination sums to N ---------------------------------------------

TEST(StarsAndBars, EachCombinationSumsToN) {
  for (int N = 0; N <= 5; ++N) {
    for (int K = 1; K <= 5; ++K) {
      StarsAndBars sb(N, K);
      for (const auto& urns : sb) {
        int s = std::accumulate(urns.begin(), urns.end(), 0);
        EXPECT_EQ(s, N) << "N=" << N << " K=" << K;
      }
    }
  }
}

// ---- Each combination has exactly K elements --------------------------------

TEST(StarsAndBars, EachCombinationHasKBins) {
  for (int N = 0; N <= 4; ++N) {
    for (int K = 1; K <= 4; ++K) {
      StarsAndBars sb(N, K);
      for (const auto& urns : sb) {
        EXPECT_EQ((int)urns.size(), K) << "N=" << N << " K=" << K;
      }
    }
  }
}

// ---- All combinations are unique --------------------------------------------

TEST(StarsAndBars, AllCombinationsUnique) {
  for (int N = 0; N <= 5; ++N) {
    for (int K = 1; K <= 4; ++K) {
      StarsAndBars sb(N, K);
      std::set<std::vector<int>> seen;
      for (const auto& urns : sb) {
        EXPECT_TRUE(seen.insert(urns).second)
            << "Duplicate for N=" << N << " K=" << K;
      }
    }
  }
}

// ---- Edge cases: K=0 --------------------------------------------------------

TEST(StarsAndBars, K0ProducesNoElements) {
  StarsAndBars sb(3, 0);
  EXPECT_EQ(sb.begin(), sb.end());
}

TEST(StarsAndBars, N0K0ProducesNoElements) {
  StarsAndBars sb(0, 0);
  EXPECT_EQ(sb.begin(), sb.end());
}

// ---- Iterator semantics -----------------------------------------------------

TEST(StarsAndBars, PostfixIncrementReturnsPreviousValue) {
  StarsAndBars sb(2, 2);
  auto it = sb.begin();
  std::vector<int> first = *it;
  auto old = it++;
  EXPECT_EQ(*old, first);
  EXPECT_NE(it, old);
}

TEST(StarsAndBars, PrefixAndPostfixAgreeFinalResult) {
  StarsAndBars sb(3, 3);
  auto it_pre = sb.begin();
  auto it_post = sb.begin();
  while (it_pre != sb.end() && it_post != sb.end()) {
    EXPECT_EQ(*it_pre, *it_post);
    ++it_pre;
    it_post++;
  }
  EXPECT_EQ(it_pre, sb.end());
  EXPECT_EQ(it_post, sb.end());
}

TEST(StarsAndBars, ArrowOperatorMatchesDereference) {
  StarsAndBars sb(2, 3);
  for (auto it = sb.begin(); it != sb.end(); ++it) {
    EXPECT_EQ(*it, *it.operator->());
  }
}

TEST(StarsAndBars, CopiedIteratorIsIndependent) {
  StarsAndBars sb(3, 2);
  auto it1 = sb.begin();
  auto it2 = it1;  // copy
  ++it1;
  // it2 should still point to the first element
  EXPECT_EQ(*it2, (std::vector<int>{3, 0}));
  EXPECT_EQ(*it1, (std::vector<int>{2, 1}));
}

TEST(StarsAndBars, EndIteratorEquality) {
  StarsAndBars sb(2, 2);
  EXPECT_EQ(sb.end(), sb.end());
}

TEST(StarsAndBars, BeginNotEqualEndWhenNonEmpty) {
  StarsAndBars sb(1, 2);
  EXPECT_NE(sb.begin(), sb.end());
}

TEST(StarsAndBars, IncrementingPastEndIsNoop) {
  StarsAndBars sb(1, 1);
  auto it = sb.begin();
  ++it;
  EXPECT_EQ(it, sb.end());
  ++it;  // incrementing end should not crash
  EXPECT_EQ(it, sb.end());
}

// ---- All non-negative values ------------------------------------------------

TEST(StarsAndBars, AllValuesNonNegative) {
  StarsAndBars sb(4, 4);
  for (const auto& urns : sb) {
    for (int v : urns) {
      EXPECT_GE(v, 0);
    }
  }
}

// ---- Invalid arguments ------------------------------------------------------

TEST(StarsAndBars, NegativeNThrows) {
  EXPECT_THROW(StarsAndBars(-1, 2), std::invalid_argument);
}

TEST(StarsAndBars, NegativeKThrows) {
  EXPECT_THROW(StarsAndBars(2, -1), std::invalid_argument);
}

}  // namespace mcts
