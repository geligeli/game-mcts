#ifndef GAME_MCTS_GAME_MCTS_GAMES_RISK_RISK_GAME_H
#define GAME_MCTS_GAME_MCTS_GAMES_RISK_RISK_GAME_H
#include <sys/types.h>

#include <algorithm>
#include <array>
#include <bit>
#include <bitset>
#include <cassert>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <iostream>
#include <limits>
#include <ostream>
#include <random>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

#if defined(__SSE2__)
#include <emmintrin.h>
#endif

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "game_mcts/core/mcts/game_traits.h"
#include "game_mcts/core/util/overloaded.h"
#include "game_mcts/games/risk/ascii/ascii_board.h"
#include "game_mcts/games/risk/risk_board.h"

namespace risk_game {

namespace internal {
// Uniform integer in [0, n) via Lemire's multiply-shift method; consumes a
// single 32-bit draw in the common case (vs. ~2 draws plus division for the
// default-range std::uniform_int_distribution, which rejects half its draws).
inline uint32_t UniformBelow(std::mt19937 &gen, uint32_t n) {
  uint64_t m = static_cast<uint64_t>(gen()) * n;
  uint32_t l = static_cast<uint32_t>(m);
  if (l < n) {
    const uint32_t threshold = static_cast<uint32_t>(-n) % n;
    while (l < threshold) {
      m = static_cast<uint64_t>(gen()) * n;
      l = static_cast<uint32_t>(m);
    }
  }
  return static_cast<uint32_t>(m >> 32);
}

// Saturating add for territory unit counts. The physical rules make the army
// supply effectively unbounded (pieces consolidate into higher denominations
// when they run out), so units stay uint16_t to keep Territory packed at 4
// bytes; saturation only guards against wraparound in pathologically long
// random rollout games, which real play never reaches.
inline uint16_t SaturatingAddUnits(uint16_t units, uint32_t add) {
  return static_cast<uint16_t>(
      std::min<uint32_t>(std::numeric_limits<uint16_t>::max(), units + add));
}
}  // namespace internal

struct Territory {
  int8_t owner;  // Supports up to 16 players
  uint16_t units;
  constexpr bool operator==(const Territory &other) const = default;
};

namespace internal {
// Fills ownership masks over the territory map: bit i of |mine| is set iff
// map[i].owner == player, bit i of |strong| iff map[i].units > 1. The SSE2
// path (baseline on x86-64) processes four territories per iteration; other
// targets use the scalar loop.
inline void OwnershipMasks(const Territory *map, size_t num_territories,
                           int8_t player, uint64_t &mine, uint64_t &strong) {
  static_assert(sizeof(Territory) == 4);
  static_assert(offsetof(Territory, owner) == 0);
  static_assert(offsetof(Territory, units) == 2);
  mine = 0;
  strong = 0;
#if defined(__SSE2__)
  const __m128i owner_broadcast =
      _mm_set1_epi32(static_cast<int32_t>(static_cast<uint8_t>(player)));
  const __m128i byte_mask = _mm_set1_epi32(0xFF);
  const __m128i one = _mm_set1_epi32(1);
  size_t i = 0;
  for (; i + 4 <= num_territories; i += 4) {
    const __m128i v =
        _mm_loadu_si128(reinterpret_cast<const __m128i *>(map + i));
    const __m128i owners = _mm_and_si128(v, byte_mask);
    mine |= static_cast<uint64_t>(static_cast<uint32_t>(_mm_movemask_ps(
                _mm_castsi128_ps(_mm_cmpeq_epi32(owners, owner_broadcast)))))
            << i;
    const __m128i units = _mm_srli_epi32(v, 16);
    strong |= static_cast<uint64_t>(static_cast<uint32_t>(_mm_movemask_ps(
                  _mm_castsi128_ps(_mm_cmpgt_epi32(units, one)))))
              << i;
  }
  for (; i < num_territories; ++i) {
    mine |= static_cast<uint64_t>(map[i].owner == player) << i;
    strong |= static_cast<uint64_t>(map[i].units > 1) << i;
  }
#else
  for (size_t i = 0; i < num_territories; ++i) {
    mine |= static_cast<uint64_t>(map[i].owner == player) << i;
    strong |= static_cast<uint64_t>(map[i].units > 1) << i;
  }
#endif
}
}  // namespace internal

struct InitialPlaceAction {
  int territory_;
  constexpr bool operator==(const InitialPlaceAction &other) const = default;
  constexpr auto operator<=>(const InitialPlaceAction &other) const = default;
};

struct ReinforceAction {
  std::array<uint16_t, kNumTerritories>
      units_to_place_;  // Indexed by territory ID
  constexpr bool operator==(const ReinforceAction &other) const = default;
  constexpr auto operator<=>(const ReinforceAction &other) const = default;
};

struct QueueAttackAction {
  // Moves every army that may on a conquest: num_move_on_conquest_ is clamped.
  static constexpr int kMoveAll = 1 << 16;

  int source_;
  int target;
  int num_attack_dice_;
  // How many armies move in if this roll conquers the target, clamped to what
  // the rules allow: at least the dice rolled, at most all but one left
  // behind. 0 (the default) moves the dice, kMoveAll everything.
  int num_move_on_conquest_ = 0;
  constexpr bool operator==(const QueueAttackAction &other) const = default;
  constexpr auto operator<=>(const QueueAttackAction &other) const = default;
};

struct PlayerAction {
  std::optional<ReinforceAction> reinforce_action_;
  std::optional<QueueAttackAction> attack_action_;
  constexpr bool operator==(const PlayerAction &other) const = default;
  constexpr auto operator<=>(const PlayerAction &other) const = default;
};

struct QueueDefenseAction {
  int num_defend_dice_;
  constexpr bool operator==(const QueueDefenseAction &other) const = default;
  constexpr auto operator<=>(const QueueDefenseAction &other) const = default;
};

struct FortifyAction {
  int source_;
  int target;
  int num_units;
  constexpr bool operator==(const FortifyAction &other) const = default;
  constexpr auto operator<=>(const FortifyAction &other) const = default;
};

struct RollDiceAction {
  std::array<int, 3> attacker_rolls_;
  std::array<int, 2> defender_rolls_;
  constexpr bool operator==(const RollDiceAction &other) const = default;
  constexpr auto operator<=>(const RollDiceAction &other) const = default;
};

using RiskAction =
    std::variant<InitialPlaceAction, PlayerAction, QueueDefenseAction,
                 FortifyAction, RollDiceAction>;

std::ostream &operator<<(std::ostream &os, const RiskAction &action);

inline constexpr size_t hash_combine(size_t seed, size_t h) {
  return seed ^ (h + 0x9e3779b9 + (seed << 6) + (seed >> 2));
}

}  // namespace risk_game

template <>
struct std::hash<risk_game::ReinforceAction> {
  size_t operator()(const risk_game::ReinforceAction &ra) const {
    // The array is 42 tightly packed uint16_t (84 bytes, no padding): mix it
    // as 64-bit words instead of a combine round per element.
    static_assert(sizeof(ra.units_to_place_) == 84);
    const char *data =
        reinterpret_cast<const char *>(ra.units_to_place_.data());
    uint64_t h = 0x9e3779b97f4a7c15ULL;
    for (size_t offset = 0; offset < 80; offset += sizeof(uint64_t)) {
      uint64_t w;
      std::memcpy(&w, data + offset, sizeof(w));
      h ^= w;
      h *= 0x9E3779B97F4A7C15ULL;
      h = std::rotl(h, 31);
    }
    uint32_t tail;
    std::memcpy(&tail, data + 80, sizeof(tail));
    h ^= tail;
    h *= 0x9E3779B97F4A7C15ULL;
    h ^= h >> 32;
    return static_cast<size_t>(h);
  }
};

template <>
struct std::hash<risk_game::RiskAction> {
  size_t operator()(const risk_game::RiskAction &action) const {
    size_t h = std::hash<size_t>{}(action.index());
    std::visit(
        overloaded{
            [&h](const risk_game::InitialPlaceAction &a) {
              h = risk_game::hash_combine(h, std::hash<int>{}(a.territory_));
            },
            [&h](const risk_game::PlayerAction &a) {
              if (a.reinforce_action_) {
                h = risk_game::hash_combine(
                    h, std::hash<risk_game::ReinforceAction>{}(
                           *a.reinforce_action_));
              }
              if (a.attack_action_) {
                h = risk_game::hash_combine(
                    h, std::hash<int>{}(a.attack_action_->source_));
                h = risk_game::hash_combine(
                    h, std::hash<int>{}(a.attack_action_->target));
                h = risk_game::hash_combine(
                    h, std::hash<int>{}(a.attack_action_->num_attack_dice_));
                h = risk_game::hash_combine(
                    h,
                    std::hash<int>{}(a.attack_action_->num_move_on_conquest_));
              }
            },
            [&h](const risk_game::QueueDefenseAction &a) {
              h = risk_game::hash_combine(h,
                                          std::hash<int>{}(a.num_defend_dice_));
            },
            [&h](const risk_game::FortifyAction &a) {
              h = risk_game::hash_combine(h, std::hash<int>{}(a.source_));
              h = risk_game::hash_combine(h, std::hash<int>{}(a.target));
              h = risk_game::hash_combine(h, std::hash<int>{}(a.num_units));
            },
            [&h](const risk_game::RollDiceAction &a) {
              for (int v : a.attacker_rolls_)
                h = risk_game::hash_combine(h, std::hash<int>{}(v));
              for (int v : a.defender_rolls_)
                h = risk_game::hash_combine(h, std::hash<int>{}(v));
            },
        },
        action);
    return h;
  }
};

namespace risk_game {

template <size_t NUM_PLAYERS>
struct RiskState;

// ANSI color digits per player index. Used as a background (40+c) for the
// board tiles and as a foreground (30+c) for player labels ("P0", "P1", ...)
// so that a player's name always matches their color on the board.
inline constexpr std::array<uint8_t, 6> kPlayerColors = {1, 2, 3, 5, 6, 7};

// SGR sequence painting text in |player|'s board color (empty for player < 0,
// i.e. chance nodes / unowned). Terminate with kColorReset.
inline std::string PlayerColor(int player) {
  if (player < 0) {
    return "";
  }
  return "\033[3" +
         std::to_string(kPlayerColors[static_cast<size_t>(player) %
                                      kPlayerColors.size()]) +
         "m";
}

inline constexpr const char *kColorReset = "\033[0m";

// Renders the state (turn header + ascii board). Territories whose index is
// set in |highlighted| (size kNumTerritories, may be empty) are drawn with a
// bright background, regardless of owner.
template <size_t NUM_PLAYERS>
std::string RenderRiskState(const RiskState<NUM_PLAYERS> &state,
                            std::span<const bool> highlighted = {});

template <size_t NUM_PLAYERS>
struct RiskState {
  static constexpr size_t kNumPlayers = NUM_PLAYERS;
  constexpr RiskState() {
    for (Territory &t : map_) {
      t = {.owner = -1, .units = 0};  // Unowned
    }
  }

  static constexpr std::array<uint16_t, NUM_PLAYERS> InitialReserves() {
    std::array<uint16_t, NUM_PLAYERS> res{};
    static_assert(NUM_PLAYERS >= 2 && NUM_PLAYERS <= 6,
                  "Supported players: 2 to 6");
    switch (NUM_PLAYERS) {
      case 2:
        std::fill(res.begin(), res.end(), 40);
        break;
      case 3:
        std::fill(res.begin(), res.end(), 35);
        break;
      case 4:
        std::fill(res.begin(), res.end(), 30);
        break;
      case 5:
        std::fill(res.begin(), res.end(), 25);
        break;
      case 6:
        std::fill(res.begin(), res.end(), 20);
        break;
      default:
        throw std::runtime_error("Unsupported number of players");
    }
    return res;
  }
  std::array<uint16_t, NUM_PLAYERS> reserves_ = InitialReserves();
  std::array<Territory, kNumTerritories> map_;
  bool initial_placement_{true};
  std::size_t num_initial_placements_{0};
  bool first_attack_of_turn_{true};
  int8_t current_player_{0};
  uint32_t turn_count_{0};

  std::optional<QueueAttackAction> queued_attack_;
  std::optional<QueueDefenseAction> queued_defense_;

  bool operator==(const RiskState &other) const = default;

  void AddTurnStartReinforcements() {
    // Ownership bitmask over the map: cheaper than a per-territory scan and
    // directly gives both the owned count and the continent checks.
    uint64_t mine;
    uint64_t strong;
    internal::OwnershipMasks(map_.data(), map_.size(), current_player_, mine,
                             strong);
    reserves_[current_player_] =
        std::max(3, static_cast<int>(std::popcount(mine)) / 3);
    for (size_t c = 0; c < risk_game::kContinents.size(); ++c) {
      if ((mine & kContinentMasks[c]) == kContinentMasks[c]) {
        reserves_[current_player_] +=
            risk_game::kContinents[c].bonus_reinforcements_;
      }
    }
  }

  void NextPlayer() {
    // Skip eliminated players (no territories owned). Terminates because the
    // game is only ongoing while at least one player owns a territory.
    do {
      current_player_ = (current_player_ + 1) % NUM_PLAYERS;
    } while (std::none_of(map_.begin(), map_.end(), [&](const Territory &t) {
      return t.owner == current_player_;
    }));
    if (current_player_ == 0) {
      turn_count_++;
    }
    AddTurnStartReinforcements();
    first_attack_of_turn_ = true;
  }

  void InitialPlace(int territory) {
    Territory &t = map_[territory];
    t.owner = current_player_;
    t.units = internal::SaturatingAddUnits(t.units, 1);
    reserves_[current_player_] -= 1;

    current_player_ = (current_player_ + 1) % NUM_PLAYERS;
    if (current_player_ == 0) {
      turn_count_++;
      if (reserves_[0] == 0) {
        initial_placement_ = false;
        // Every turn starts with reinforcements, the first one included;
        // later turns get theirs in NextPlayer().
        AddTurnStartReinforcements();
      }
    }
    num_initial_placements_++;
  }

  void ReinforceWithReserves(const ReinforceAction &reinforce_action) {
#pragma GCC unroll 42
    for (size_t i = 0; i < map_.size(); ++i) {
      if (reinforce_action.units_to_place_[i] > 0) {
        Territory &t = map_[i];
        assert(t.owner == current_player_);
        assert(reserves_[current_player_] >=
               reinforce_action.units_to_place_[i]);
        t.units = internal::SaturatingAddUnits(
            t.units, reinforce_action.units_to_place_[i]);
        reserves_[current_player_] -= reinforce_action.units_to_place_[i];
      }
    }
  }

  void FortifyToMoveUnits(FortifyAction fortify_action) {
    if (fortify_action.num_units <= 1) {
      NextPlayer();
      return;
    }
    assert(fortify_action.source_ >= 0 &&
           fortify_action.source_ < static_cast<int>(map_.size()));
    assert(fortify_action.target >= 0 &&
           fortify_action.target < static_cast<int>(map_.size()));
    assert(fortify_action.num_units > 0);
    assert(map_[fortify_action.source_].units >= fortify_action.num_units);
    map_[fortify_action.source_].units -= fortify_action.num_units;
    map_[fortify_action.target].units = internal::SaturatingAddUnits(
        map_[fortify_action.target].units, fortify_action.num_units);
    NextPlayer();
  }

  void Attack(QueueAttackAction attack_action) {
    assert(attack_action.source_ != attack_action.target);
    assert(attack_action.source_ >= 0 &&
           attack_action.source_ < static_cast<int>(map_.size()));
    assert(attack_action.target >= 0 &&
           attack_action.target < static_cast<int>(map_.size()));
    assert(attack_action.num_attack_dice_ >= 1 &&
           attack_action.num_attack_dice_ <= 3);
    assert(map_[attack_action.source_].units > attack_action.num_attack_dice_);
    assert(!queued_attack_.has_value());
    assert(!queued_defense_.has_value());
    queued_attack_ = attack_action;
    current_player_ = map_[attack_action.target].owner;
    first_attack_of_turn_ = false;
  }

  void ResolveAttackRolls(const RollDiceAction &roll_action) {
    assert(queued_attack_.has_value());
    assert(queued_defense_.has_value());

    Territory &src = map_[queued_attack_->source_];
    Territory &tgt = map_[queued_attack_->target];
    std::array<int, 3> attacker_rolls = roll_action.attacker_rolls_;
    std::array<int, 2> defender_rolls = roll_action.defender_rolls_;

    std::sort(attacker_rolls.begin(), attacker_rolls.end(),
              std::greater<int>());
    std::sort(defender_rolls.begin(), defender_rolls.end(),
              std::greater<int>());

    for (const auto &[attacker, defender] :
         std::views::zip(attacker_rolls, defender_rolls)) {
      if (attacker == 0 || defender == 0) {
        break;
      }
      if (attacker > defender) {
        tgt.units -= 1;
      } else {
        src.units -= 1;
      }
    }

    if (tgt.units == 0) {
      // At least the dice rolled, at most all but one left behind.
      const int most = static_cast<int>(src.units) - 1;
      const int least = std::min(queued_attack_->num_attack_dice_, most);
      const int asked = queued_attack_->num_move_on_conquest_;
      const int move_units =
          std::clamp(asked == 0 ? least : asked, least, most);
      src.units -= move_units;
      // tgt was just reduced to 0, so this cannot saturate in practice; use
      // the saturating add anyway to keep "all unit growth is saturating".
      tgt.units = internal::SaturatingAddUnits(tgt.units, move_units);
      tgt.owner = map_[queued_attack_->source_].owner;
    }
    current_player_ = map_[queued_attack_->source_].owner;
    queued_attack_ = std::nullopt;
    queued_defense_ = std::nullopt;
  }

  using action_t = RiskAction;

  friend std::ostream &operator<<(std::ostream &os, const RiskState &state) {
    os << RenderRiskState(state);
    return os;
  }

  int current_player() const { return current_player_; }

  // In-place variant of apply_action: same transitions, but mutates this
  // state instead of copying it. Hot loops that operate on a scratch state
  // (e.g. MCTS rollouts) should prefer this; apply_action() remains the
  // canonical value-semantics interface.
  void apply_action_in_place(const action_t &action) {
    std::visit(
        overloaded{
            [&](const InitialPlaceAction &act) {
              InitialPlace(act.territory_);
            },
            [&](const PlayerAction &act) {
              assert(initial_placement_ == false);
              assert(current_player_ >= 0 &&
                     current_player_ < static_cast<int32_t>(NUM_PLAYERS));
              if (act.reinforce_action_.has_value()) {
                ReinforceWithReserves(*act.reinforce_action_);
              }
              if (act.attack_action_.has_value()) {
                Attack(*act.attack_action_);
              }
            },
            [&](const QueueDefenseAction &act) {
              assert(!queued_defense_.has_value());
              assert(queued_attack_.has_value());
              queued_defense_ = act;
              current_player_ = -1;
            },
            [&](const RollDiceAction &act) { ResolveAttackRolls(act); },
            [&](const FortifyAction &act) { FortifyToMoveUnits(act); },
        },
        action);
  }

  RiskState<NUM_PLAYERS> apply_action(const action_t &action) const {
    auto next = *this;
    next.apply_action_in_place(action);
    return next;
  }

  mcts::game_state_t current_state() const {
    int seen_owner = -1;
    for (const Territory &t : map_) {
      if (t.owner == -1) {
        return mcts::ongoing_t{};
      }
      if (seen_owner == -1) {
        seen_owner = t.owner;
      } else if (t.owner != seen_owner) {
        return mcts::ongoing_t{};
      }
    }
    return mcts::win_t{.winning_player = seen_owner};
  }

  // A battle with both sides committed is the only chance node in Risk: the
  // attacker has queued the attack and the defender has answered with a dice
  // count, so nothing is left to decide.
  bool is_chance_node() const { return current_player_ == -1; }

  // Samples the rules-defined chance distribution (the dice). This is game
  // logic, not policy: MCTS builds its chance-node children straight from it,
  // so it deliberately is not swappable the way an ActionProposer is.
  // Precondition: is_chance_node().
  action_t sample_chance_action(std::mt19937 &gen) const {
    assert(queued_attack_.has_value() && queued_defense_.has_value());
    std::array<int, 3> attacker_rolls = {0, 0, 0};
    std::array<int, 2> defender_rolls = {0, 0};
    const int num_attack_dice = queued_attack_->num_attack_dice_;
    const int num_defend_dice = queued_defense_->num_defend_dice_;
    for (int i = 0; i < num_attack_dice; ++i) {
      attacker_rolls[i] = static_cast<int>(gen() % 6) + 1;
    }
    for (int i = 0; i < num_defend_dice; ++i) {
      defender_rolls[i] = static_cast<int>(gen() % 6) + 1;
    }
    return RollDiceAction{attacker_rolls, defender_rolls};
  }

  // Legality oracle for referee/debug paths (not hot loops; apply_action
  // stays unchecked). Returns true iff applying |action| to this state is
  // legal under the engine's current transition semantics. On failure sets
  // |reason| to a short human-readable explanation; on success leaves it
  // untouched.
  bool is_valid_action(const action_t &action, std::string &reason) const {
    return std::visit(
        overloaded{
            [&](const InitialPlaceAction &act) -> bool {
              if (!initial_placement_) {
                reason = "initial placement phase is over";
                return false;
              }
              if (act.territory_ < 0 ||
                  act.territory_ >= static_cast<int>(map_.size())) {
                reason = "territory out of range";
                return false;
              }
              if (num_initial_placements_ < kNumTerritories) {
                if (map_[act.territory_].owner != -1) {
                  reason = "territory already owned during claiming phase";
                  return false;
                }
              } else if (map_[act.territory_].owner != current_player_) {
                reason =
                    "can only reinforce own territories during initial "
                    "placement";
                return false;
              }
              if (reserves_[current_player_] == 0) {
                reason = "no reserves left to place";
                return false;
              }
              return true;
            },
            [&](const PlayerAction &act) -> bool {
              if (initial_placement_) {
                reason = "still in initial placement phase";
                return false;
              }
              if (queued_attack_.has_value()) {
                reason = "a queued attack must be resolved first";
                return false;
              }
              if (current_player_ < 0 ||
                  current_player_ >= static_cast<int>(NUM_PLAYERS)) {
                reason = "no player to move at this node";
                return false;
              }
              // Either would leave the state as it was with the same player
              // to move, so a bot could repeat it until the referee's move
              // cap: a turn ends with a FortifyAction.
              if (!act.reinforce_action_.has_value() &&
                  !act.attack_action_.has_value()) {
                reason =
                    "a PlayerAction must reinforce or attack; end the turn "
                    "with a FortifyAction";
                return false;
              }
              if (act.reinforce_action_.has_value()) {
                if (reserves_[current_player_] == 0) {
                  reason = "no reserves left to place";
                  return false;
                }
                if (!first_attack_of_turn_) {
                  reason =
                      "reinforcements must be placed before the first "
                      "attack of the turn";
                  return false;
                }
                uint32_t sum = 0;
                for (size_t i = 0; i < map_.size(); ++i) {
                  if (act.reinforce_action_->units_to_place_[i] == 0) {
                    continue;
                  }
                  if (map_[i].owner != current_player_) {
                    reason =
                        "cannot reinforce a territory not owned by the "
                        "current player";
                    return false;
                  }
                  sum += act.reinforce_action_->units_to_place_[i];
                }
                if (sum != reserves_[current_player_]) {
                  reason = "all reserve armies must be placed";
                  return false;
                }
              }
              if (act.attack_action_.has_value()) {
                const QueueAttackAction &atk = *act.attack_action_;
                if (atk.source_ < 0 ||
                    atk.source_ >= static_cast<int>(map_.size()) ||
                    atk.target < 0 ||
                    atk.target >= static_cast<int>(map_.size())) {
                  reason = "attack source/target out of range";
                  return false;
                }
                if (atk.source_ == atk.target) {
                  reason = "attack source and target must differ";
                  return false;
                }
                if (map_[atk.source_].owner != current_player_) {
                  reason = "attack source not owned by the current player";
                  return false;
                }
                if (!((kNeighborMasks[atk.source_] >> atk.target) & 1)) {
                  reason = "attack target not adjacent to source";
                  return false;
                }
                if (map_[atk.target].owner == current_player_) {
                  reason = "cannot attack own territory";
                  return false;
                }
                if (atk.num_attack_dice_ < 1 || atk.num_attack_dice_ > 3) {
                  reason = "num_attack_dice out of range [1, 3]";
                  return false;
                }
                if (atk.num_move_on_conquest_ < 0) {
                  reason = "num_move_on_conquest must not be negative";
                  return false;
                }
                if (map_[atk.source_].units <=
                    static_cast<uint32_t>(atk.num_attack_dice_)) {
                  reason =
                      "attack source must keep one army behind (units "
                      "must exceed num_attack_dice)";
                  return false;
                }
              }
              return true;
            },
            [&](const QueueDefenseAction &act) -> bool {
              if (!queued_attack_.has_value() || queued_defense_.has_value()) {
                reason = "no attack awaiting a defense";
                return false;
              }
              if (current_player_ != map_[queued_attack_->target].owner) {
                reason = "only the defender of the queued attack may defend";
                return false;
              }
              if (act.num_defend_dice_ < 1 || act.num_defend_dice_ > 2) {
                reason = "num_defend_dice out of range [1, 2]";
                return false;
              }
              if (act.num_defend_dice_ >
                  static_cast<int>(map_[queued_attack_->target].units)) {
                reason = "cannot defend with more dice than defending armies";
                return false;
              }
              return true;
            },
            [&](const RollDiceAction &act) -> bool {
              if (!queued_attack_.has_value() || !queued_defense_.has_value()) {
                reason = "no battle awaiting dice rolls";
                return false;
              }
              for (size_t i = 0; i < act.attacker_rolls_.size(); ++i) {
                const bool used =
                    i < static_cast<size_t>(queued_attack_->num_attack_dice_);
                if (used && (act.attacker_rolls_[i] < 1 ||
                             act.attacker_rolls_[i] > 6)) {
                  reason = "attacker roll out of range [1, 6]";
                  return false;
                }
                if (!used && act.attacker_rolls_[i] != 0) {
                  reason = "unused attacker roll slots must be 0";
                  return false;
                }
              }
              for (size_t i = 0; i < act.defender_rolls_.size(); ++i) {
                const bool used =
                    i < static_cast<size_t>(queued_defense_->num_defend_dice_);
                if (used && (act.defender_rolls_[i] < 1 ||
                             act.defender_rolls_[i] > 6)) {
                  reason = "defender roll out of range [1, 6]";
                  return false;
                }
                if (!used && act.defender_rolls_[i] != 0) {
                  reason = "unused defender roll slots must be 0";
                  return false;
                }
              }
              return true;
            },
            [&](const FortifyAction &act) -> bool {
              if (initial_placement_) {
                reason = "still in initial placement phase";
                return false;
              }
              if (queued_attack_.has_value() || queued_defense_.has_value()) {
                reason = "a queued battle must be resolved first";
                return false;
              }
              if (current_player_ < 0 ||
                  current_player_ >= static_cast<int>(NUM_PLAYERS)) {
                reason = "no player to move at this node";
                return false;
              }
              // num_units <= 1 means "skip fortify / end turn" and is always
              // legal (see FortifyToMoveUnits).
              if (act.num_units <= 1) {
                return true;
              }
              if (act.source_ < 0 ||
                  act.source_ >= static_cast<int>(map_.size()) ||
                  act.target < 0 ||
                  act.target >= static_cast<int>(map_.size())) {
                reason = "fortify source/target out of range";
                return false;
              }
              if (map_[act.source_].owner != current_player_ ||
                  map_[act.target].owner != current_player_) {
                reason = "can only fortify between own territories";
                return false;
              }
              if (act.source_ == act.target) {
                // No-op move the engine accepts (FortifyToMoveUnits subtracts
                // then re-adds); sample_action emits it when the player owns
                // a single territory. The engine only requires the units to
                // be present in this case.
                if (map_[act.source_].units <
                    static_cast<uint32_t>(act.num_units)) {
                  reason = "fortify moves more units than the source has";
                  return false;
                }
                return true;
              }
              if (map_[act.source_].units <
                  static_cast<uint32_t>(act.num_units) + 1) {
                reason = "fortify must leave at least one army behind";
                return false;
              }
              return true;
            },
        },
        action);
  }

  std::bitset<kNumTerritories> GetConnectedComponent(size_t seed) {
    std::bitset<kNumTerritories> result{};
    std::array<size_t, kNumTerritories> stack;
    auto stack_ptr = stack.begin();

    auto push = [&](size_t elem) { *(stack_ptr++) = elem; };
    auto pop = [&]() { return *(--stack_ptr); };

    result.set(seed);
    push(seed);

    while (stack_ptr != stack.begin()) {
      auto currnet_elem = pop();
      const CountryData &country_data = kBoard[currnet_elem];
      for (size_t i = 0; i < country_data.neighbor_count_; ++i) {
        size_t neighbor = static_cast<size_t>(country_data.neighbors_[i]);
        if (!result.test(neighbor) &&
            map_[currnet_elem].owner == map_[neighbor].owner) {
          result.set(neighbor);
          push(neighbor);
        }
      }
    }
    return result;
  }
};

template <size_t NUM_PLAYERS>
std::string RenderRiskState(const RiskState<NUM_PLAYERS> &state,
                            std::span<const bool> highlighted) {
  const int current_player = static_cast<int>(state.current_player_);
  std::ostringstream os;
  os << "Turn: " << state.turn_count_
     << " Player: " << PlayerColor(current_player) << current_player
     << kColorReset << "\n";
  os << "Reserves: ";
  for (size_t p = 0; p < NUM_PLAYERS; ++p)
    os << PlayerColor(static_cast<int>(p)) << "P" << p << kColorReset << ":"
       << state.reserves_[p] << " ";
  os << "\n";
  os << "Units: ";
  std::array<int, NUM_PLAYERS> total_units{};
  for (const Territory &t : state.map_) {
    if (t.owner >= 0) {
      total_units[static_cast<size_t>(t.owner)] += t.units;
    }
  }
  for (size_t p = 0; p < NUM_PLAYERS; ++p)
    os << PlayerColor(static_cast<int>(p)) << "P" << p << kColorReset << ":"
       << total_units[p] << " ";
  os << "\n";

  static constexpr int board_width = 80;
  static constexpr int text_width = 3;
  static const auto segments = GetAsciiBoardTemplate(board_width, text_width);
  if (segments.empty()) {
    for (size_t i = 0; i < state.map_.size(); ++i) {
      if (const int owner = static_cast<int>(state.map_[i].owner);
          owner != -1) {
        os << "  T" << i << ": " << PlayerColor(owner) << "P" << owner
           << kColorReset << " (" << state.map_[i].units << ")\n";
      }
    }
    return os.str();
  }

  std::array<uint8_t, 43> colors{};
  colors[0] = 4;  // background = blue
  std::array<int, 43> troop_counts{};

  const bool highlight = highlighted.size() == state.map_.size();
  for (size_t i = 0; i < state.map_.size(); ++i) {
    uint8_t tid = kCountryToTerritoryId[i];
    uint8_t color =
        state.map_[i].owner >= 0
            ? kPlayerColors[static_cast<size_t>(state.map_[i].owner) %
                            kPlayerColors.size()]
            : 0;  // unowned = black
    // Bright variant highlights the territory regardless of owner.
    if (highlight && highlighted[i]) {
      color += 8;
    }
    colors[tid] = color;
    troop_counts[tid] = state.map_[i].units;
  }

  os << RenderAsciiBoard(segments, colors, troop_counts) << "\n";
  return os.str();
}

static_assert(mcts::ChanceGame<risk_game::RiskState<2>>);
// mcts::PlayoutStep detects and prefers the in-place transition over the
// copying apply_action(), which matters in the rollout hot loop.
static_assert(mcts::InPlaceGame<risk_game::RiskState<2>>);

}  // namespace risk_game

#endif  // GAME_MCTS_GAME_MCTS_GAMES_RISK_RISK_GAME_H
