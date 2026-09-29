#ifndef GAME_MCTS_GAME_MCTS_GAMES_RISK_RISK_RENDER_H
#define GAME_MCTS_GAME_MCTS_GAMES_RISK_RISK_RENDER_H

// One step of a Risk game, for a person: a caption saying what happened, and
// the board with what the step touched highlighted and an arrow from source to
// target. Shared by the arena's replays (problem/risk_session.h) and the
// terminal (problem/risk_replay.cc).

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <sstream>
#include <string>
#include <variant>
#include <vector>

#include "game_mcts/core/util/overloaded.h"
#include "game_mcts/games/risk/ascii/ascii_board.h"
#include "game_mcts/games/risk/risk_board.h"
#include "game_mcts/games/risk/risk_game.h"

namespace risk_game {

// What a step touched, drawn on the board.
struct BoardMarks {
  std::array<bool, kNumTerritories> highlighted_{};
  int arrow_from_ = -1;  // territory index; -1: no arrow
  int arrow_to_ = -1;
};

// The 80-column board from colours and troop counts indexed by template
// territory id (see RenderAsciiBoard), with |marks| drawn on it. Empty when
// the template is missing.
std::string RenderMarkedBoard(std::span<const uint8_t> colors,
                              std::span<const int> troops,
                              const BoardMarks &marks);

namespace render_internal {

inline std::string Paint(int player, std::string text, bool color) {
  return color && player >= 0 ? PlayerColor(player) + text + kColorReset : text;
}

inline std::string Bold(const std::string &text, bool color) {
  return color ? "\x1b[1m" + text + "\x1b[22m" : text;
}

inline std::string Player(int player, bool color) {
  return Paint(player, "P" + std::to_string(player), color);
}

// A territory's name in its owner's colour.
template <size_t NUM_PLAYERS>
std::string Named(const RiskState<NUM_PLAYERS> &state, int territory,
                  bool color) {
  return Paint(state.map_[territory].owner,
               std::string(kBoard[territory].name_), color);
}

template <size_t NUM_PLAYERS>
std::string WithUnits(const RiskState<NUM_PLAYERS> &state, int territory,
                      bool color) {
  return Named(state, territory, color) + "(" +
         std::to_string(state.map_[territory].units) + ")";
}

inline std::string Dice(int count) {
  return std::to_string(count) + (count == 1 ? " die" : " dice");
}

}  // namespace render_internal

// A round of dice: the pairs compared highest against highest (ties to the
// defender), the losses, and a conquest if there was one.
template <size_t NUM_PLAYERS>
std::string DescribeRoll(const RiskState<NUM_PLAYERS> &before,
                         const RollDiceAction &roll,
                         const RiskState<NUM_PLAYERS> &after, bool color) {
  using render_internal::Bold;
  using render_internal::Named;
  if (!before.queued_attack_.has_value()) {
    return "dice";
  }
  const int source = before.queued_attack_->source_;
  const int target = before.queued_attack_->target;
  auto attacker = roll.attacker_rolls_;
  auto defender = roll.defender_rolls_;
  std::ranges::sort(attacker, std::greater<>());
  std::ranges::sort(defender, std::greater<>());
  std::ostringstream out;
  const auto faces = [&out](std::span<const int> rolls) {
    const char *separator = "";
    for (const int face : rolls) {
      if (face > 0) {
        out << separator << face;
        separator = " ";
      }
    }
  };
  out << "dice A[";
  faces(attacker);
  out << "] D[";
  faces(defender);
  out << "]:";
  const char *separator = " ";
  for (std::size_t i = 0; i < defender.size(); ++i) {
    if (attacker[i] == 0 || defender[i] == 0) {
      break;
    }
    const bool hit = attacker[i] > defender[i];
    out << separator << attacker[i] << (hit ? ">" : "<=") << defender[i]
        << (hit ? " D-1" : " A-1");
    separator = ", ";
  }
  if (after.map_[target].owner != before.map_[target].owner) {
    out << " | " << Bold(Named(after, target, color) + " CONQUERED", color)
        << ": " << after.map_[target].units << " move in ("
        << Named(after, source, color) << " " << after.map_[source].units
        << ", " << Named(after, target, color) << " "
        << after.map_[target].units << ")";
  } else {
    out << " -> " << Named(after, source, color) << " "
        << after.map_[source].units << ", " << Named(after, target, color)
        << " " << after.map_[target].units;
  }
  return std::move(out).str();
}

// One line saying what |action| did to |before| (giving |after|).
template <size_t NUM_PLAYERS>
std::string DescribeStep(const RiskState<NUM_PLAYERS> &before,
                         const RiskAction &action,
                         const RiskState<NUM_PLAYERS> &after,
                         bool color = true) {
  using render_internal::Dice;
  using render_internal::Named;
  using render_internal::Player;
  using render_internal::WithUnits;
  const int player = before.current_player_;
  std::ostringstream out;
  std::visit(
      overloaded{
          [&](const InitialPlaceAction &place) {
            const int t = place.territory_;
            out << Player(player, color)
                << (before.map_[t].owner < 0
                        ? " claims " + Named(after, t, color)
                        : " +1 " + WithUnits(after, t, color));
          },
          [&](const PlayerAction &move) {
            out << Player(player, color);
            if (move.reinforce_action_.has_value()) {
              out << " places";
              const char *separator = " ";
              for (int t = 0; t < kNumTerritories; ++t) {
                const auto units = move.reinforce_action_->units_to_place_[t];
                if (units > 0) {
                  out << separator << Named(after, t, color) << " +" << units
                      << " (" << after.map_[t].units << ")";
                  separator = ", ";
                }
              }
            }
            if (move.attack_action_.has_value()) {
              const QueueAttackAction &attack = *move.attack_action_;
              out << (move.reinforce_action_.has_value() ? " |" : "")
                  << " attacks " << WithUnits(after, attack.source_, color)
                  << " ==> " << WithUnits(after, attack.target, color) << ", "
                  << Dice(attack.num_attack_dice_);
              if (attack.num_move_on_conquest_ >= QueueAttackAction::kMoveAll) {
                out << ", all move in on a win";
              } else if (attack.num_move_on_conquest_ > 0) {
                out << ", " << attack.num_move_on_conquest_
                    << " move in on a win";
              }
            }
          },
          [&](const QueueDefenseAction &defense) {
            out << Player(player, color) << " defends ";
            if (before.queued_attack_.has_value()) {
              out << WithUnits(before, before.queued_attack_->target, color)
                  << " ";
            }
            out << "with " << Dice(defense.num_defend_dice_);
          },
          [&](const RollDiceAction &roll) {
            out << DescribeRoll(before, roll, after, color);
          },
          [&](const FortifyAction &fortify) {
            out << Player(player, color);
            if (fortify.num_units <= 1) {
              out << " ends turn";
              return;
            }
            out << " fortifies " << WithUnits(before, fortify.source_, color)
                << " ==> " << WithUnits(before, fortify.target, color)
                << ": moves " << fortify.num_units << " ("
                << Named(after, fortify.source_, color) << " "
                << after.map_[fortify.source_].units << ", "
                << Named(after, fortify.target, color) << " "
                << after.map_[fortify.target].units << ")";
          },
      },
      action);
  return std::move(out).str();
}

// What a step touched: placements, and the source and target of an attack
// (its defence and dice too) or a fortify, with an arrow between them.
template <size_t NUM_PLAYERS>
BoardMarks StepMarks(const RiskState<NUM_PLAYERS> &before,
                     const RiskAction &action) {
  BoardMarks marks;
  const auto arrow = [&marks](int from, int to) {
    marks.highlighted_[from] = marks.highlighted_[to] = true;
    marks.arrow_from_ = from;
    marks.arrow_to_ = to;
  };
  const auto queued = [&] {
    if (before.queued_attack_.has_value()) {
      arrow(before.queued_attack_->source_, before.queued_attack_->target);
    }
  };
  std::visit(overloaded{
                 [&](const InitialPlaceAction &place) {
                   marks.highlighted_[place.territory_] = true;
                 },
                 [&](const PlayerAction &move) {
                   if (move.reinforce_action_.has_value()) {
                     for (int t = 0; t < kNumTerritories; ++t) {
                       marks.highlighted_[t] =
                           move.reinforce_action_->units_to_place_[t] > 0;
                     }
                   }
                   if (move.attack_action_.has_value()) {
                     arrow(move.attack_action_->source_,
                           move.attack_action_->target);
                   }
                 },
                 [&](const QueueDefenseAction &) { queued(); },
                 [&](const RollDiceAction &) { queued(); },
                 [&](const FortifyAction &fortify) {
                   if (fortify.num_units > 1) {
                     arrow(fortify.source_, fortify.target);
                   }
                 },
             },
             action);
  return marks;
}

// |state|'s board with |marks|, owners in their colours, the marked
// territories bright.
template <size_t NUM_PLAYERS>
std::string RenderBoard(const RiskState<NUM_PLAYERS> &state,
                        const BoardMarks &marks) {
  std::array<uint8_t, 43> colors{};
  colors[0] = 4;  // the sea
  std::array<int, 43> troops{};
  for (int t = 0; t < kNumTerritories; ++t) {
    const uint8_t id = kCountryToTerritoryId[t];
    const int owner = state.map_[t].owner;
    colors[id] =
        owner >= 0
            ? kPlayerColors[static_cast<size_t>(owner) % kPlayerColors.size()]
            : 0;
    if (marks.highlighted_[t]) {
      colors[id] += 8;
    }
    troops[id] = state.map_[t].units;
  }
  return RenderMarkedBoard(colors, troops, marks);
}

// A player's battles over one turn, for the line that ends it.
struct TurnTally {
  int battles_ = 0;
  int lost_ = 0;
  int killed_ = 0;
  std::vector<int> taken_;

  template <size_t NUM_PLAYERS>
  void Add(const RiskState<NUM_PLAYERS> &before, const RiskAction &action,
           const RiskState<NUM_PLAYERS> &after) {
    if (!std::holds_alternative<RollDiceAction>(action) ||
        !before.queued_attack_.has_value()) {
      return;
    }
    const int source = before.queued_attack_->source_;
    const int target = before.queued_attack_->target;
    const bool taken = after.map_[target].owner != before.map_[target].owner;
    const int moved = taken ? after.map_[target].units : 0;
    ++battles_;
    lost_ += before.map_[source].units - after.map_[source].units - moved;
    killed_ += before.map_[target].units -
               (taken ? 0 : static_cast<int>(after.map_[target].units));
    if (taken) {
      taken_.push_back(target);
    }
  }

  std::string Describe(int player, bool color) const {
    std::ostringstream out;
    out << render_internal::Player(player, color) << "'s turn: ";
    if (battles_ == 0) {
      out << "no battles";
      return std::move(out).str();
    }
    out << battles_ << (battles_ == 1 ? " battle" : " battles") << ", took "
        << taken_.size();
    if (!taken_.empty()) {
      out << " (";
      for (std::size_t i = 0; i < taken_.size(); ++i) {
        out << (i > 0 ? ", " : "") << kBoard[taken_[i]].name_;
      }
      out << ")";
    }
    out << ", lost " << lost_ << ", killed " << killed_;
    return std::move(out).str();
  }
};

}  // namespace risk_game

#endif  // GAME_MCTS_GAME_MCTS_GAMES_RISK_RISK_RENDER_H
