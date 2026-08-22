#include "game_mcts/cpp/risk/risk_game.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <random>
#include <unordered_set>
#include <vector>

#include "absl/debugging/failure_signal_handler.h"
#include "absl/debugging/symbolize.h"
#include "absl/log/log.h"
#include "game_mcts/cpp/risk/strategies/risk_proposer.h"
#include "game_mcts/cpp/risk/strategies/risk_rollout_shortcuts.h"
#include "game_mcts/cpp/mcts/mcts.inl"

namespace risk_game {

namespace {
// The stock proposal policy, used wherever these tests need "some legal move".
constexpr RiskProposer<2> kProposer{};
// One playout step: dice at chance nodes, kProposer at decision nodes.
auto SampleAction(const RiskState<2> &state,
                  std::mt19937 &gen) -> RiskState<2>::action_t {
  return state.is_chance_node() ? state.sample_chance_action(gen)
                                : kProposer.sample(state, gen);
}
}  // namespace

// Helper to check if a vector contains an element
template <typename T>
auto Contains(const std::vector<T> &vec, const T &element) -> bool {
  return std::find(vec.begin(), vec.end(), element) != vec.end();
}

TEST(RiskGameTest, InitialReinforcements) {
  RiskState<2> state;

  std::mt19937 gen(42);

  EXPECT_EQ(state.current_player(), 0);

  auto action = SampleAction(state, gen);
  state = state.apply_action(action);
  EXPECT_EQ(state.current_player(), 1);

  action = SampleAction(state, gen);
  state = state.apply_action(action);
  EXPECT_EQ(state.current_player(), 0);
}

TEST(RiskGameTest, RunGameWithRandomActions) {
  RiskState<2> state;

  std::mt19937 gen(42);

  auto start_reserves = state.m_reserves;
  for (int i = 0; i < 40; ++i) {
    auto action = SampleAction(state, gen);
    state = state.apply_action(action);
    ASSERT_EQ(state.current_player(), 1);
    ASSERT_EQ(start_reserves[0] - state.m_reserves[0], i + 1)
        << i << " " << action;
    action = SampleAction(state, gen);
    state = state.apply_action(action);
    ASSERT_EQ(state.current_player(), 0);
    ASSERT_EQ(start_reserves[1] - state.m_reserves[1], i + 1)
        << i << " " << action;
  }

  for (int i = 0; i < 5000; ++i) {
    auto action = SampleAction(state, gen);
    state = state.apply_action(action);
    if (mcts::is_terminal(state.current_state())) {
      break;
    }
  }
}

TEST(RiskGameTest, BattleExpectationShortcutResolvesQueuedBattle) {
  RiskState<2> state;
  std::mt19937 gen(42);

  // The shortcut does not apply to decision nodes.
  EXPECT_FALSE(ResolveBattleWithExpectation(state, gen).has_value());

  // Play random actions until a battle is fully queued (chance node).
  while (!state.is_chance_node()) {
    mcts::PlayoutStep(state, kProposer, gen);
  }
  ASSERT_TRUE(state.queued_attack.has_value());
  ASSERT_TRUE(state.queued_defense.has_value());

  const int src = state.queued_attack->source;
  const int tgt = state.queued_attack->target;
  const int attackers = state.m_map[src].units - 1;
  const int defenders = state.m_map[tgt].units;
  const int8_t attacker_owner = state.m_map[src].owner;

  const auto next_opt = ResolveBattleWithExpectation(state, gen);
  ASSERT_TRUE(next_opt.has_value());
  const RiskState<2> &next = *next_opt;

  EXPECT_FALSE(next.queued_attack.has_value());
  EXPECT_FALSE(next.queued_defense.has_value());
  EXPECT_EQ(next.m_current_player, attacker_owner);

  const BattleRemnants expected = LookupExpectedRemnants(attackers, defenders);
  // Winner = side with more expected survivors.
  if (expected.attackers > expected.defenders) {
    EXPECT_EQ(next.m_map[tgt].owner, attacker_owner);
    EXPECT_EQ(next.m_map[tgt].units, expected.attackers);
    EXPECT_EQ(next.m_map[src].units,
              state.m_map[src].units - (attackers - expected.attackers));
  } else {
    EXPECT_EQ(next.m_map[src].units, 1);
    EXPECT_EQ(next.m_map[src].owner, attacker_owner);
    EXPECT_EQ(next.m_map[tgt].units, expected.defenders);
  }
}

TEST(RiskGameTest, ShortcutRolloutPlaysFullGame) {
  std::mt19937 gen(42);
  const auto policy = mcts::MakeShortcutRollout<RiskState<2>, RiskProposer<2>>(
      &ResolveBattleWithExpectation<2>);
  static_assert(mcts::RolloutPolicy<decltype(policy), RiskState<2>>);
  const auto result = policy(RiskState<2>{}, gen);
  // A full rollout must reach a terminal state; Risk has no draws.
  EXPECT_TRUE(std::holds_alternative<mcts::win_t>(result));

  // Same for the in-place form of the shortcut.
  const auto in_place_policy =
      mcts::MakeShortcutRollout<RiskState<2>, RiskProposer<2>>(
          &ResolveBattleWithExpectationInPlace<2>);
  static_assert(mcts::RolloutPolicy<decltype(in_place_policy), RiskState<2>>);
  const auto in_place_result = in_place_policy(RiskState<2>{}, gen);
  EXPECT_TRUE(std::holds_alternative<mcts::win_t>(in_place_result));
}

TEST(RiskGameTest, NextPlayerSkipsEliminatedPlayers) {
  RiskState<3> state;
  // Players 0 and 2 own one territory each; player 1 is eliminated.
  state.m_initial_placement = false;
  state.m_map[0].owner = 0;
  state.m_map[0].units = 2;
  state.m_map[1].owner = 2;
  state.m_map[1].units = 2;
  state.m_current_player = 0;

  // Passing on the fortify step advances to the next player.
  state.FortifyToMoveUnits(
      FortifyAction{.source = 0, .target = 0, .num_units = 1});
  EXPECT_EQ(state.current_player(), 2);
}

// Every action the proposer (and the game's own chance sampler) produces must
// validate as legal at the moment it is sampled, over many seeded random games
// covering all phases (initial placement, reinforce, attack, defense, dice
// rolls, fortify).
TEST(RiskGameTest, SampledActionsAreAlwaysLegal) {
  std::mt19937 gen(123);
  std::string reason;
  for (int game_idx = 0; game_idx < 200; ++game_idx) {
    RiskState<2> state;
    for (int step = 0; step < 20000; ++step) {
      const auto action = SampleAction(state, gen);
      ASSERT_TRUE(state.is_valid_action(action, reason))
          << "game " << game_idx << " step " << step << ": " << reason
          << ", action: " << action;
      state = state.apply_action(action);
      if (mcts::is_terminal(state.current_state())) {
        break;
      }
    }
  }
}

// RiskProposer::support_size() is a contract: DedupSampler stops drawing the
// moment that many distinct actions have been handed out, so an under-report
// silently costs the search children. Brute-force it — for every state of a
// batch of random games, draw far more than the claimed support and check the
// distinct count lands exactly on it.
TEST(RiskGameTest, SupportSizeMatchesBruteForceSampling) {
  std::mt19937 gen(2024);
  int bounded_states = 0;
  int unbounded_states = 0;
  for (int game_idx = 0; game_idx < 20; ++game_idx) {
    RiskState<2> state;
    for (int step = 0; step < 400; ++step) {
      if (mcts::is_terminal(state.current_state())) {
        break;
      }
      if (!state.is_chance_node()) {
        const std::size_t claimed = kProposer.support_size(state);
        if (claimed == mcts::kUnknownSupport) {
          ++unbounded_states;
        } else {
          ++bounded_states;
          // 200x the claimed support makes missing a reachable action
          // vanishingly unlikely even for the largest branches here.
          const std::size_t draws =
              std::min<std::size_t>(200000, claimed * 200);
          std::unordered_set<RiskState<2>::action_t> distinct;
          std::mt19937 probe(gen());
          for (std::size_t i = 0; i < draws; ++i) {
            distinct.insert(kProposer.sample(state, probe));
          }
          EXPECT_EQ(distinct.size(), claimed)
              << "game " << game_idx << " step " << step << " after " << draws
              << " draws";
        }
      }
      state = state.apply_action(SampleAction(state, gen));
    }
  }
  // The batch has to actually cover both kinds of branch to mean anything.
  EXPECT_GT(bounded_states, 100);
  EXPECT_GT(unbounded_states, 10);
}

// The deterministic branches (defense, and the fortify that ends a turn) must
// report a support of exactly 1, which is what lets DedupSampler exhaust them
// without spending a collision budget.
TEST(RiskGameTest, DeterministicBranchesHaveSupportOne) {
  std::mt19937 gen(7);
  RiskState<2> state;
  int defense_states = 0;
  for (int step = 0; step < 20000 && defense_states < 20; ++step) {
    if (mcts::is_terminal(state.current_state())) {
      break;
    }
    if (state.queued_attack.has_value() && !state.queued_defense.has_value()) {
      EXPECT_EQ(kProposer.support_size(state), 1u);
      ++defense_states;
    }
    state = state.apply_action(SampleAction(state, gen));
  }
  EXPECT_EQ(defense_states, 20);
}

namespace {

// A mid-game state: initial placement over, alternating ownership, 5 units
// everywhere, player 0 to move at the start of their turn with no reserves.
// Territory indices equal Country enum ordinals: Afghanistan=0 borders
// China=7, India=16, Middle_East=22, Ukraine=37, Ural=38 (all odd -> player
// 1), so 0->7 is a legal attack and 0->1 (Alaska) is non-adjacent.
auto MakeMidGameState() -> RiskState<2> {
  RiskState<2> state;
  state.m_initial_placement = false;
  state.m_num_initial_placements = kNumTerritories;
  state.m_reserves = {0, 0};
  for (size_t i = 0; i < state.m_map.size(); ++i) {
    state.m_map[i].owner = static_cast<int8_t>(i % 2);
    state.m_map[i].units = 5;
  }
  state.m_current_player = 0;
  state.m_first_attack_of_turn = true;
  return state;
}

// State with an attack queued against territory 0 (owned by player 0, who is
// the defender to move).
auto MakeQueuedAttackState() -> RiskState<2> {
  RiskState<2> state = MakeMidGameState();
  state.queued_attack =
      QueueAttackAction{.source = 7, .target = 0, .num_attack_dice = 1};
  state.m_current_player = state.m_map[0].owner;
  state.m_first_attack_of_turn = false;
  return state;
}

}  // namespace

TEST(RiskGameTest, IsValidActionInitialPlacement) {
  std::string reason;
  RiskState<2> state;

  // Claiming an unowned territory is legal.
  EXPECT_TRUE(state.is_valid_action(InitialPlaceAction{0}, reason));
  // Out-of-range territory is rejected.
  EXPECT_FALSE(state.is_valid_action(InitialPlaceAction{-1}, reason));
  EXPECT_FALSE(reason.empty());
  EXPECT_FALSE(
      state.is_valid_action(InitialPlaceAction{kNumTerritories}, reason));
  EXPECT_FALSE(reason.empty());

  // Initial-place on an already-owned territory is rejected while unclaimed
  // territories remain.
  state = state.apply_action(InitialPlaceAction{0});
  EXPECT_FALSE(state.is_valid_action(InitialPlaceAction{0}, reason));
  EXPECT_FALSE(reason.empty());

  // After the claiming phase, players may only reinforce their own
  // territories.
  RiskState<2> late;
  late.m_num_initial_placements = kNumTerritories;
  for (size_t i = 0; i < late.m_map.size(); ++i) {
    late.m_map[i].owner = static_cast<int8_t>(i % 2);
    late.m_map[i].units = 1;
  }
  late.m_current_player = 0;
  EXPECT_TRUE(late.is_valid_action(InitialPlaceAction{0}, reason));
  EXPECT_FALSE(late.is_valid_action(InitialPlaceAction{1}, reason));
  EXPECT_FALSE(reason.empty());

  // No initial placement once the phase is over.
  RiskState<2> mid = MakeMidGameState();
  EXPECT_FALSE(mid.is_valid_action(InitialPlaceAction{0}, reason));
  EXPECT_FALSE(reason.empty());
}

TEST(RiskGameTest, IsValidActionAttack) {
  std::string reason;
  const RiskState<2> state = MakeMidGameState();

  const auto attack = [](int source, int target, int dice) {
    PlayerAction pa;
    pa.attack_action = QueueAttackAction{source, target, dice};
    return RiskAction{pa};
  };

  // Sanity: a normal attack is legal.
  EXPECT_TRUE(state.is_valid_action(attack(0, 7, 3), reason));
  // Out of range and self-attack.
  EXPECT_FALSE(state.is_valid_action(attack(-1, 7, 1), reason));
  EXPECT_FALSE(reason.empty());
  EXPECT_FALSE(state.is_valid_action(attack(0, 0, 1), reason));
  EXPECT_FALSE(reason.empty());
  // Attack from a source not owned by the current player.
  EXPECT_FALSE(state.is_valid_action(attack(1, 0, 1), reason));
  EXPECT_FALSE(reason.empty());
  // Non-adjacent target.
  EXPECT_FALSE(state.is_valid_action(attack(0, 1, 1), reason));
  EXPECT_FALSE(reason.empty());
  // Attacking own (adjacent) territory.
  RiskState<2> own_target = state;
  own_target.m_map[7].owner = 0;
  EXPECT_FALSE(own_target.is_valid_action(attack(0, 7, 1), reason));
  EXPECT_FALSE(reason.empty());
  // Dice count 0 and 4 are out of [1, 3].
  EXPECT_FALSE(state.is_valid_action(attack(0, 7, 0), reason));
  EXPECT_FALSE(reason.empty());
  EXPECT_FALSE(state.is_valid_action(attack(0, 7, 4), reason));
  EXPECT_FALSE(reason.empty());
  // Source units must strictly exceed the dice count.
  RiskState<2> thin = state;
  thin.m_map[0].units = 3;
  EXPECT_FALSE(thin.is_valid_action(attack(0, 7, 3), reason));
  EXPECT_FALSE(reason.empty());
  EXPECT_TRUE(thin.is_valid_action(attack(0, 7, 2), reason));
  // A queued attack means it is no longer this player's decision node.
  const RiskState<2> queued = MakeQueuedAttackState();
  EXPECT_FALSE(queued.is_valid_action(attack(2, 7, 1), reason));
  EXPECT_FALSE(reason.empty());
}

TEST(RiskGameTest, IsValidActionReinforce) {
  std::string reason;
  RiskState<2> state = MakeMidGameState();
  state.m_reserves[0] = 3;

  const auto reinforce = [](int territory, uint16_t units) {
    PlayerAction pa;
    ReinforceAction ra{};
    ra.units_to_place[territory] = units;
    pa.reinforce_action = ra;
    return RiskAction{pa};
  };

  // Placing exactly all reserves on own territories is legal.
  EXPECT_TRUE(state.is_valid_action(reinforce(0, 3), reason));
  // Reinforcing an enemy territory is rejected.
  EXPECT_FALSE(state.is_valid_action(reinforce(1, 3), reason));
  EXPECT_FALSE(reason.empty());
  // Sum must equal the reserves (all new armies must be placed).
  EXPECT_FALSE(state.is_valid_action(reinforce(0, 2), reason));
  EXPECT_FALSE(reason.empty());
  EXPECT_FALSE(state.is_valid_action(reinforce(0, 4), reason));
  EXPECT_FALSE(reason.empty());
  // No reinforcements after the first attack of the turn.
  state.m_first_attack_of_turn = false;
  EXPECT_FALSE(state.is_valid_action(reinforce(0, 3), reason));
  EXPECT_FALSE(reason.empty());
}

TEST(RiskGameTest, IsValidActionDefense) {
  std::string reason;
  RiskState<2> state = MakeQueuedAttackState();

  // Defending with 1 or 2 dice is legal.
  EXPECT_TRUE(state.is_valid_action(QueueDefenseAction{1}, reason));
  EXPECT_TRUE(state.is_valid_action(QueueDefenseAction{2}, reason));
  // 0 and 3 dice are out of [1, 2].
  EXPECT_FALSE(state.is_valid_action(QueueDefenseAction{0}, reason));
  EXPECT_FALSE(reason.empty());
  EXPECT_FALSE(state.is_valid_action(QueueDefenseAction{3}, reason));
  EXPECT_FALSE(reason.empty());
  // Cannot defend with more dice than defending armies.
  state.m_map[0].units = 1;
  EXPECT_FALSE(state.is_valid_action(QueueDefenseAction{2}, reason));
  EXPECT_FALSE(reason.empty());
  // No defense without a queued attack.
  const RiskState<2> no_attack = MakeMidGameState();
  EXPECT_FALSE(no_attack.is_valid_action(QueueDefenseAction{1}, reason));
  EXPECT_FALSE(reason.empty());
}

TEST(RiskGameTest, IsValidActionRollDice) {
  std::string reason;
  RiskState<2> state = MakeQueuedAttackState();
  state.queued_defense = QueueDefenseAction{2};
  state.m_current_player = -1;  // Chance node.

  // Legal roll: 1 attack die, 2 defense dice, unused slots 0.
  EXPECT_TRUE(state.is_valid_action(RollDiceAction{{3, 0, 0}, {2, 4}}, reason));
  // Rolls out of [1, 6].
  EXPECT_FALSE(
      state.is_valid_action(RollDiceAction{{0, 0, 0}, {2, 4}}, reason));
  EXPECT_FALSE(reason.empty());
  EXPECT_FALSE(
      state.is_valid_action(RollDiceAction{{7, 0, 0}, {2, 4}}, reason));
  EXPECT_FALSE(reason.empty());
  EXPECT_FALSE(
      state.is_valid_action(RollDiceAction{{3, 0, 0}, {0, 4}}, reason));
  EXPECT_FALSE(reason.empty());
  // Unused roll slots must be 0.
  EXPECT_FALSE(
      state.is_valid_action(RollDiceAction{{3, 1, 0}, {2, 4}}, reason));
  EXPECT_FALSE(reason.empty());
  // No roll without a fully queued battle.
  const RiskState<2> attack_only = MakeQueuedAttackState();
  EXPECT_FALSE(
      attack_only.is_valid_action(RollDiceAction{{3, 0, 0}, {2, 4}}, reason));
  EXPECT_FALSE(reason.empty());
  const RiskState<2> no_attack = MakeMidGameState();
  EXPECT_FALSE(
      no_attack.is_valid_action(RollDiceAction{{3, 0, 0}, {2, 4}}, reason));
  EXPECT_FALSE(reason.empty());
}

TEST(RiskGameTest, IsValidActionFortify) {
  std::string reason;
  const RiskState<2> state = MakeMidGameState();

  const auto fortify = [](int source, int target, int units) {
    return RiskAction{FortifyAction{source, target, units}};
  };

  // num_units <= 1 means "skip fortify / end turn" and is always legal,
  // even with degenerate source/target.
  EXPECT_TRUE(state.is_valid_action(fortify(0, 0, 1), reason));
  EXPECT_TRUE(state.is_valid_action(fortify(0, 0, 0), reason));
  // Normal fortify between own territories.
  EXPECT_TRUE(state.is_valid_action(fortify(0, 2, 4), reason));
  // A source == target fortify is a no-op the engine accepts (sample_action
  // emits it when the player owns a single territory); it only requires the
  // units to be present.
  EXPECT_TRUE(state.is_valid_action(fortify(0, 0, 2), reason));
  EXPECT_FALSE(state.is_valid_action(fortify(0, 0, 6), reason));
  EXPECT_FALSE(reason.empty());
  // Fortify from a source not owned by the current player.
  EXPECT_FALSE(state.is_valid_action(fortify(1, 2, 2), reason));
  EXPECT_FALSE(reason.empty());
  // Fortify to a target not owned by the current player.
  EXPECT_FALSE(state.is_valid_action(fortify(0, 3, 2), reason));
  EXPECT_FALSE(reason.empty());
  // Must leave at least one army behind.
  EXPECT_FALSE(state.is_valid_action(fortify(0, 2, 5), reason));
  EXPECT_FALSE(reason.empty());
  // In-range territories.
  EXPECT_FALSE(state.is_valid_action(fortify(0, kNumTerritories, 2), reason));
  EXPECT_FALSE(reason.empty());
  // No fortify while a battle is queued.
  const RiskState<2> queued = MakeQueuedAttackState();
  EXPECT_FALSE(queued.is_valid_action(fortify(0, 2, 2), reason));
  EXPECT_FALSE(reason.empty());
}

}  // namespace risk_game
