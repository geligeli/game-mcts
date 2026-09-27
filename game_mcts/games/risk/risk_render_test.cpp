#include "game_mcts/games/risk/risk_render.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <regex>
#include <string>

#include "game_mcts/games/risk/risk_game.h"

namespace risk_game {
namespace {

constexpr int kAlaska = static_cast<int>(Country::Alaska);
constexpr int kAlberta = static_cast<int>(Country::Alberta);
constexpr int kKamchatka = static_cast<int>(Country::Kamchatka);

// Past placement, player 0 to move with 3 armies to place: Alaska (5) and
// Alberta (1) are player 0's, Kamchatka (3) and everything else player 1's.
RiskState<2> Midgame() {
  RiskState<2> state;
  state.initial_placement_ = false;
  state.num_initial_placements_ = 80;
  state.current_player_ = 0;
  state.reserves_ = {3, 0};
  for (int t = 0; t < kNumTerritories; ++t) {
    state.map_[t] = {.owner = 1, .units = 2};
  }
  state.map_[kAlaska] = {.owner = 0, .units = 5};
  state.map_[kAlberta] = {.owner = 0, .units = 1};
  state.map_[kKamchatka] = {.owner = 1, .units = 3};
  return state;
}

// Applies |action|, returning its plain caption and folding it into |tally|.
std::string Step(RiskState<2> &state, const RiskAction &action,
                 TurnTally *tally = nullptr) {
  const RiskState<2> before = state;
  state.apply_action_in_place(action);
  if (tally != nullptr) {
    tally->Add(before, action, state);
  }
  return DescribeStep(before, action, state, /*color=*/false);
}

RiskAction Attack(int source, int target, int dice) {
  return PlayerAction{.attack_action_ =
                          QueueAttackAction{source, target, dice}};
}

RiskAction Roll(std::array<int, 3> attacker, std::array<int, 2> defender) {
  return RollDiceAction{attacker, defender};
}

// Only SGR sequences (ESC [ digits;... m): anything else and the dashboard
// shows "not text" instead of the view.
bool OnlySgr(const std::string &text) {
  static const std::regex escape("\x1b(\\[[0-9;]*m)?");
  for (auto it = std::sregex_iterator(text.begin(), text.end(), escape);
       it != std::sregex_iterator(); ++it) {
    if (!(*it)[1].matched) {
      return false;
    }
  }
  return true;
}

TEST(RiskRenderTest, ClaimsAndInitialPlacements) {
  RiskState<2> state;
  EXPECT_EQ(Step(state, InitialPlaceAction{kAlaska}), "P0 claims Alaska");
  state.current_player_ = 0;
  EXPECT_EQ(Step(state, InitialPlaceAction{kAlaska}), "P0 +1 Alaska(2)");
}

TEST(RiskRenderTest, ATurnOfBattlesSaysWhatEachStepDid) {
  RiskState<2> state = Midgame();
  TurnTally tally;
  ReinforceAction reinforce{};
  reinforce.units_to_place_[kAlaska] = 3;
  EXPECT_EQ(Step(state,
                 PlayerAction{.reinforce_action_ = reinforce,
                              .attack_action_ =
                                  QueueAttackAction{kAlaska, kKamchatka, 3}},
                 &tally),
            "P0 places Alaska +3 (8) | attacks Alaska(8) ==> Kamchatka(3), 3 "
            "dice");
  EXPECT_EQ(Step(state, QueueDefenseAction{2}, &tally),
            "P1 defends Kamchatka(3) with 2 dice");
  // Highest against highest; a tie goes to the defender.
  EXPECT_EQ(Step(state, Roll({4, 6, 5}, {5, 5}), &tally),
            "dice A[6 5 4] D[5 5]: 6>5 D-1, 5<=5 A-1 -> Alaska 7, "
            "Kamchatka 2");

  EXPECT_EQ(Step(state, Attack(kAlaska, kKamchatka, 3), &tally),
            "P0 attacks Alaska(7) ==> Kamchatka(2), 3 dice");
  Step(state, QueueDefenseAction{2}, &tally);
  EXPECT_EQ(Step(state, Roll({6, 1, 6}, {1, 1}), &tally),
            "dice A[6 6 1] D[1 1]: 6>1 D-1, 6>1 D-1 | Kamchatka CONQUERED: 3 "
            "move in (Alaska 4, Kamchatka 3)");

  EXPECT_EQ(Step(state, FortifyAction{kAlaska, kAlberta, 3}),
            "P0 fortifies Alaska(4) ==> Alberta(1): moves 3 (Alaska 1, "
            "Alberta 4)");
  EXPECT_EQ(tally.Describe(0, /*color=*/false),
            "P0's turn: 2 battles, took 1 (Kamchatka), lost 1, killed 3");
  EXPECT_EQ(Step(state, FortifyAction{0, 0, 1}), "P1 ends turn");
}

TEST(RiskRenderTest, CaptionsColourNamesByOwnerWithSgrOnly) {
  RiskState<2> state = Midgame();
  const RiskState<2> before = state;
  const RiskAction attack = Attack(kAlaska, kKamchatka, 3);
  state.apply_action_in_place(attack);
  const std::string caption = DescribeStep(before, attack, state);
  EXPECT_NE(caption.find(PlayerColor(0) + "Alaska" + kColorReset),
            std::string::npos)
      << caption;
  EXPECT_TRUE(OnlySgr(caption));
}

TEST(RiskRenderTest, MarksWhatTheStepTouched) {
  const RiskState<2> state = Midgame();
  const BoardMarks marks = StepMarks(state, Attack(kAlaska, kKamchatka, 3));
  EXPECT_EQ(std::ranges::count(marks.highlighted_, true), 2);
  EXPECT_TRUE(marks.highlighted_[kAlaska] && marks.highlighted_[kKamchatka]);
  EXPECT_EQ(marks.arrow_from_, kAlaska);
  EXPECT_EQ(marks.arrow_to_, kKamchatka);
  EXPECT_EQ(StepMarks(state, FortifyAction{0, 0, 1}).arrow_from_, -1);
}

TEST(RiskRenderTest, TheBoardMarksWhatTheStepTouched) {
  const RiskState<2> state = Midgame();
  const std::string board =
      RenderBoard(state, StepMarks(state, FortifyAction{kAlaska, kAlberta, 3}));
  ASSERT_FALSE(board.empty());
  EXPECT_TRUE(OnlySgr(board));
  EXPECT_NE(board.find("\x1b[10"), std::string::npos) << "marked is bright";
}

TEST(RiskRenderTest, ArrowsPointFromSourceToTarget) {
  const RiskState<2> state = Midgame();
  // Alaska sits at the left edge and Kamchatka at the right: the arrow leaves
  // Alaska westwards and arrives at Kamchatka from the east.
  const std::string across =
      RenderBoard(state, StepMarks(state, Attack(kAlaska, kKamchatka, 3)));
  EXPECT_NE(across.find("←"), std::string::npos);
  EXPECT_EQ(across.find("→"), std::string::npos);

  BoardMarks marks;
  marks.arrow_from_ = kAlaska;
  marks.arrow_to_ = static_cast<int>(Country::Argentina);
  const std::string south = RenderBoard(state, marks);
  EXPECT_TRUE(south.find("↓") != std::string::npos ||
              south.find("↘") != std::string::npos)
      << south;
}

}  // namespace
}  // namespace risk_game
