#ifndef RISK_GAME_AI_CPP_RISK_RISK_GAME_H
#define RISK_GAME_AI_CPP_RISK_RISK_GAME_H
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
#include "cpp/risk/ascii_board.h"
#include "cpp/risk/risk_board.h"
#include "game_mcts/cpp/mcts/game_traits.h"
#include "game_mcts/cpp/mcts/overloaded.h"

namespace risk_game {

namespace internal {
// Uniform integer in [0, n) via Lemire's multiply-shift method; consumes a
// single 32-bit draw in the common case (vs. ~2 draws plus division for the
// default-range std::uniform_int_distribution, which rejects half its draws).
inline auto UniformBelow(std::mt19937 &gen, uint32_t n) -> uint32_t {
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
inline auto SaturatingAddUnits(uint16_t units, uint32_t add) -> uint16_t {
  return static_cast<uint16_t>(
      std::min<uint32_t>(std::numeric_limits<uint16_t>::max(), units + add));
}
}  // namespace internal

struct Territory {
  int8_t owner;  // Supports up to 16 players
  uint16_t units;
  constexpr auto operator==(const Territory &other) const -> bool = default;
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
  int territory;
  constexpr auto operator==(const InitialPlaceAction &other) const
      -> bool = default;
  constexpr auto operator<=>(const InitialPlaceAction &other) const = default;
};

struct ReinforceAction {
  std::array<uint16_t, kNumTerritories>
      units_to_place;  // Indexed by territory ID
  constexpr auto operator==(const ReinforceAction &other) const -> bool =
                                                                       default;
  constexpr auto operator<=>(const ReinforceAction &other) const = default;
};

struct QueueAttackAction {
  int source;
  int target;
  int num_attack_dice;
  constexpr auto operator==(const QueueAttackAction &other) const
      -> bool = default;
  constexpr auto operator<=>(const QueueAttackAction &other) const = default;
};

struct PlayerAction {
  std::optional<ReinforceAction> reinforce_action;
  std::optional<QueueAttackAction> attack_action;
  constexpr auto operator==(const PlayerAction &other) const -> bool = default;
  constexpr auto operator<=>(const PlayerAction &other) const = default;
};

struct QueueDefenseAction {
  int num_defend_dice;
  constexpr auto operator==(const QueueDefenseAction &other) const
      -> bool = default;
  constexpr auto operator<=>(const QueueDefenseAction &other) const = default;
};

struct FortifyAction {
  int source;
  int target;
  int num_units;
  constexpr auto operator==(const FortifyAction &other) const -> bool = default;
  constexpr auto operator<=>(const FortifyAction &other) const = default;
};

struct RollDiceAction {
  std::array<int, 3> attacker_rolls;
  std::array<int, 2> defender_rolls;
  constexpr auto operator==(const RollDiceAction &other) const -> bool =
                                                                      default;
  constexpr auto operator<=>(const RollDiceAction &other) const = default;
};

using RiskAction =
    std::variant<InitialPlaceAction, PlayerAction, QueueDefenseAction,
                 FortifyAction, RollDiceAction>;

auto operator<<(std::ostream &os, const RiskAction &action) -> std::ostream &;

inline constexpr auto hash_combine(size_t seed, size_t h) -> size_t {
  return seed ^ (h + 0x9e3779b9 + (seed << 6) + (seed >> 2));
}

}  // namespace risk_game

template <>
struct std::hash<risk_game::ReinforceAction> {
  auto operator()(const risk_game::ReinforceAction &ra) const -> size_t {
    // The array is 42 tightly packed uint16_t (84 bytes, no padding): mix it
    // as 64-bit words instead of a combine round per element.
    static_assert(sizeof(ra.units_to_place) == 84);
    const char *data = reinterpret_cast<const char *>(ra.units_to_place.data());
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
  auto operator()(const risk_game::RiskAction &action) const -> size_t {
    size_t h = std::hash<size_t>{}(action.index());
    std::visit(
        overloaded{
            [&h](const risk_game::InitialPlaceAction &a) {
              h = risk_game::hash_combine(h, std::hash<int>{}(a.territory));
            },
            [&h](const risk_game::PlayerAction &a) {
              if (a.reinforce_action) {
                h = risk_game::hash_combine(
                    h, std::hash<risk_game::ReinforceAction>{}(
                           *a.reinforce_action));
              }
              if (a.attack_action) {
                h = risk_game::hash_combine(
                    h, std::hash<int>{}(a.attack_action->source));
                h = risk_game::hash_combine(
                    h, std::hash<int>{}(a.attack_action->target));
                h = risk_game::hash_combine(
                    h, std::hash<int>{}(a.attack_action->num_attack_dice));
              }
            },
            [&h](const risk_game::QueueDefenseAction &a) {
              h = risk_game::hash_combine(h,
                                          std::hash<int>{}(a.num_defend_dice));
            },
            [&h](const risk_game::FortifyAction &a) {
              h = risk_game::hash_combine(h, std::hash<int>{}(a.source));
              h = risk_game::hash_combine(h, std::hash<int>{}(a.target));
              h = risk_game::hash_combine(h, std::hash<int>{}(a.num_units));
            },
            [&h](const risk_game::RollDiceAction &a) {
              for (int v : a.attacker_rolls)
                h = risk_game::hash_combine(h, std::hash<int>{}(v));
              for (int v : a.defender_rolls)
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
inline auto PlayerColor(int player) -> std::string {
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
auto RenderRiskState(const RiskState<NUM_PLAYERS> &state,
                     std::span<const bool> highlighted = {}) -> std::string;

template <size_t NUM_PLAYERS>
struct RiskState {
  static constexpr size_t kNumPlayers = NUM_PLAYERS;
  constexpr RiskState() {
    for (Territory &t : m_map) {
      t = {.owner = -1, .units = 0};  // Unowned
    }
  }

  static constexpr auto initial_reserves()
      -> std::array<uint16_t, NUM_PLAYERS> {
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
  std::array<uint16_t, NUM_PLAYERS> m_reserves = initial_reserves();
  std::array<Territory, kNumTerritories> m_map;
  bool m_initial_placement{true};
  std::size_t m_num_initial_placements{0};
  bool m_first_attack_of_turn{true};
  int8_t m_current_player{0};
  uint32_t turn_count{0};

  std::optional<QueueAttackAction> queued_attack;
  std::optional<QueueDefenseAction> queued_defense;

  auto operator==(const RiskState &other) const -> bool = default;

  void AddTurnStartReinforcements() {
    // Ownership bitmask over the map: cheaper than a per-territory scan and
    // directly gives both the owned count and the continent checks.
    uint64_t mine;
    uint64_t strong;
    internal::OwnershipMasks(m_map.data(), m_map.size(), m_current_player, mine,
                             strong);
    m_reserves[m_current_player] =
        std::max(3, static_cast<int>(std::popcount(mine)) / 3);
    for (size_t c = 0; c < risk_game::Continents.size(); ++c) {
      if ((mine & kContinentMasks[c]) == kContinentMasks[c]) {
        m_reserves[m_current_player] +=
            risk_game::Continents[c].bonus_reinforcements;
      }
    }
  }

  void NextPlayer() {
    // Skip eliminated players (no territories owned). Terminates because the
    // game is only ongoing while at least one player owns a territory.
    do {
      m_current_player = (m_current_player + 1) % NUM_PLAYERS;
    } while (std::none_of(m_map.begin(), m_map.end(), [&](const Territory &t) {
      return t.owner == m_current_player;
    }));
    if (m_current_player == 0) {
      turn_count++;
    }
    AddTurnStartReinforcements();
    m_first_attack_of_turn = true;
  }

  void InitialPlace(int territory) {
    Territory &t = m_map[territory];
    t.owner = m_current_player;
    t.units = internal::SaturatingAddUnits(t.units, 1);
    m_reserves[m_current_player] -= 1;

    m_current_player = (m_current_player + 1) % NUM_PLAYERS;
    if (m_current_player == 0) {
      turn_count++;
      if (m_reserves[0] == 0) {
        m_initial_placement = false;
      }
    }
    m_num_initial_placements++;
  }

  void ReinforceWithReserves(const ReinforceAction &reinforce_action) {
#pragma GCC unroll 42
    for (size_t i = 0; i < m_map.size(); ++i) {
      if (reinforce_action.units_to_place[i] > 0) {
        Territory &t = m_map[i];
        assert(t.owner == m_current_player);
        assert(m_reserves[m_current_player] >=
               reinforce_action.units_to_place[i]);
        t.units = internal::SaturatingAddUnits(
            t.units, reinforce_action.units_to_place[i]);
        m_reserves[m_current_player] -= reinforce_action.units_to_place[i];
      }
    }
  }

  void FortifyToMoveUnits(FortifyAction fortify_action) {
    if (fortify_action.num_units <= 1) {
      NextPlayer();
      return;
    }
    assert(fortify_action.source >= 0 &&
           fortify_action.source < static_cast<int>(m_map.size()));
    assert(fortify_action.target >= 0 &&
           fortify_action.target < static_cast<int>(m_map.size()));
    assert(fortify_action.num_units > 0);
    assert(m_map[fortify_action.source].units >= fortify_action.num_units);
    m_map[fortify_action.source].units -= fortify_action.num_units;
    m_map[fortify_action.target].units = internal::SaturatingAddUnits(
        m_map[fortify_action.target].units, fortify_action.num_units);
    NextPlayer();
  }

  void Attack(QueueAttackAction attack_action) {
    assert(attack_action.source != attack_action.target);
    assert(attack_action.source >= 0 &&
           attack_action.source < static_cast<int>(m_map.size()));
    assert(attack_action.target >= 0 &&
           attack_action.target < static_cast<int>(m_map.size()));
    assert(attack_action.num_attack_dice >= 1 &&
           attack_action.num_attack_dice <= 3);
    assert(m_map[attack_action.source].units > attack_action.num_attack_dice);
    assert(!queued_attack.has_value());
    assert(!queued_defense.has_value());
    queued_attack = attack_action;
    m_current_player = m_map[attack_action.target].owner;
    m_first_attack_of_turn = false;
  }

  void ResolveAttackRolls(const RollDiceAction &roll_action) {
    assert(queued_attack.has_value());
    assert(queued_defense.has_value());

    Territory &src = m_map[queued_attack->source];
    Territory &tgt = m_map[queued_attack->target];
    std::array<int, 3> attacker_rolls = roll_action.attacker_rolls;
    std::array<int, 2> defender_rolls = roll_action.defender_rolls;

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
      int move_units = std::min(queued_attack->num_attack_dice, src.units - 1);
      src.units -= move_units;
      // tgt was just reduced to 0, so this cannot saturate in practice; use
      // the saturating add anyway to keep "all unit growth is saturating".
      tgt.units = internal::SaturatingAddUnits(tgt.units, move_units);
      tgt.owner = m_map[queued_attack->source].owner;
    }
    m_current_player = m_map[queued_attack->source].owner;
    queued_attack = std::nullopt;
    queued_defense = std::nullopt;
  }

  using action_t = RiskAction;

  friend auto operator<<(std::ostream &os,
                         const RiskState &state) -> std::ostream & {
    os << RenderRiskState(state);
    return os;
  }

  auto current_player() const -> int { return m_current_player; }

  // In-place variant of apply_action: same transitions, but mutates this
  // state instead of copying it. Hot loops that operate on a scratch state
  // (e.g. MCTS rollouts) should prefer this; apply_action() remains the
  // canonical value-semantics interface.
  void apply_action_in_place(const action_t &action) {
    std::visit(
        overloaded{
            [&](const InitialPlaceAction &act) { InitialPlace(act.territory); },
            [&](const PlayerAction &act) {
              assert(m_initial_placement == false);
              assert(m_current_player >= 0 &&
                     m_current_player < static_cast<int32_t>(NUM_PLAYERS));
              if (act.reinforce_action.has_value()) {
                ReinforceWithReserves(*act.reinforce_action);
              }
              if (act.attack_action.has_value()) {
                Attack(*act.attack_action);
              }
            },
            [&](const QueueDefenseAction &act) {
              assert(!queued_defense.has_value());
              assert(queued_attack.has_value());
              queued_defense = act;
              m_current_player = -1;
            },
            [&](const RollDiceAction &act) { ResolveAttackRolls(act); },
            [&](const FortifyAction &act) { FortifyToMoveUnits(act); },
        },
        action);
  }

  auto apply_action(const action_t &action) const -> RiskState<NUM_PLAYERS> {
    auto next = *this;
    next.apply_action_in_place(action);
    return next;
  }

  auto current_state() const -> mcts::game_state_t {
    int seen_owner = -1;
    for (const Territory &t : m_map) {
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
  auto is_chance_node() const -> bool { return m_current_player == -1; }

  // Samples the rules-defined chance distribution (the dice). This is game
  // logic, not policy: MCTS builds its chance-node children straight from it,
  // so it deliberately is not swappable the way an ActionProposer is.
  // Precondition: is_chance_node().
  auto sample_chance_action(std::mt19937 &gen) const -> action_t {
    assert(queued_attack.has_value() && queued_defense.has_value());
    std::array<int, 3> attacker_rolls = {0, 0, 0};
    std::array<int, 2> defender_rolls = {0, 0};
    const int num_attack_dice = queued_attack->num_attack_dice;
    const int num_defend_dice = queued_defense->num_defend_dice;
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
  auto is_valid_action(const action_t &action,
                       std::string &reason) const -> bool {
    return std::visit(
        overloaded{
            [&](const InitialPlaceAction &act) -> bool {
              if (!m_initial_placement) {
                reason = "initial placement phase is over";
                return false;
              }
              if (act.territory < 0 ||
                  act.territory >= static_cast<int>(m_map.size())) {
                reason = "territory out of range";
                return false;
              }
              if (m_num_initial_placements < kNumTerritories) {
                if (m_map[act.territory].owner != -1) {
                  reason = "territory already owned during claiming phase";
                  return false;
                }
              } else if (m_map[act.territory].owner != m_current_player) {
                reason =
                    "can only reinforce own territories during initial "
                    "placement";
                return false;
              }
              if (m_reserves[m_current_player] == 0) {
                reason = "no reserves left to place";
                return false;
              }
              return true;
            },
            [&](const PlayerAction &act) -> bool {
              if (m_initial_placement) {
                reason = "still in initial placement phase";
                return false;
              }
              if (queued_attack.has_value()) {
                reason = "a queued attack must be resolved first";
                return false;
              }
              if (m_current_player < 0 ||
                  m_current_player >= static_cast<int>(NUM_PLAYERS)) {
                reason = "no player to move at this node";
                return false;
              }
              if (act.reinforce_action.has_value()) {
                if (!m_first_attack_of_turn) {
                  reason =
                      "reinforcements must be placed before the first "
                      "attack of the turn";
                  return false;
                }
                uint32_t sum = 0;
                for (size_t i = 0; i < m_map.size(); ++i) {
                  if (act.reinforce_action->units_to_place[i] == 0) {
                    continue;
                  }
                  if (m_map[i].owner != m_current_player) {
                    reason =
                        "cannot reinforce a territory not owned by the "
                        "current player";
                    return false;
                  }
                  sum += act.reinforce_action->units_to_place[i];
                }
                if (sum != m_reserves[m_current_player]) {
                  reason = "all reserve armies must be placed";
                  return false;
                }
              }
              if (act.attack_action.has_value()) {
                const QueueAttackAction &atk = *act.attack_action;
                if (atk.source < 0 ||
                    atk.source >= static_cast<int>(m_map.size()) ||
                    atk.target < 0 ||
                    atk.target >= static_cast<int>(m_map.size())) {
                  reason = "attack source/target out of range";
                  return false;
                }
                if (atk.source == atk.target) {
                  reason = "attack source and target must differ";
                  return false;
                }
                if (m_map[atk.source].owner != m_current_player) {
                  reason = "attack source not owned by the current player";
                  return false;
                }
                if (!((kNeighborMasks[atk.source] >> atk.target) & 1)) {
                  reason = "attack target not adjacent to source";
                  return false;
                }
                if (m_map[atk.target].owner == m_current_player) {
                  reason = "cannot attack own territory";
                  return false;
                }
                if (atk.num_attack_dice < 1 || atk.num_attack_dice > 3) {
                  reason = "num_attack_dice out of range [1, 3]";
                  return false;
                }
                if (m_map[atk.source].units <=
                    static_cast<uint32_t>(atk.num_attack_dice)) {
                  reason =
                      "attack source must keep one army behind (units "
                      "must exceed num_attack_dice)";
                  return false;
                }
              }
              return true;
            },
            [&](const QueueDefenseAction &act) -> bool {
              if (!queued_attack.has_value() || queued_defense.has_value()) {
                reason = "no attack awaiting a defense";
                return false;
              }
              if (m_current_player != m_map[queued_attack->target].owner) {
                reason = "only the defender of the queued attack may defend";
                return false;
              }
              if (act.num_defend_dice < 1 || act.num_defend_dice > 2) {
                reason = "num_defend_dice out of range [1, 2]";
                return false;
              }
              if (act.num_defend_dice >
                  static_cast<int>(m_map[queued_attack->target].units)) {
                reason = "cannot defend with more dice than defending armies";
                return false;
              }
              return true;
            },
            [&](const RollDiceAction &act) -> bool {
              if (!queued_attack.has_value() || !queued_defense.has_value()) {
                reason = "no battle awaiting dice rolls";
                return false;
              }
              for (size_t i = 0; i < act.attacker_rolls.size(); ++i) {
                const bool used =
                    i < static_cast<size_t>(queued_attack->num_attack_dice);
                if (used &&
                    (act.attacker_rolls[i] < 1 || act.attacker_rolls[i] > 6)) {
                  reason = "attacker roll out of range [1, 6]";
                  return false;
                }
                if (!used && act.attacker_rolls[i] != 0) {
                  reason = "unused attacker roll slots must be 0";
                  return false;
                }
              }
              for (size_t i = 0; i < act.defender_rolls.size(); ++i) {
                const bool used =
                    i < static_cast<size_t>(queued_defense->num_defend_dice);
                if (used &&
                    (act.defender_rolls[i] < 1 || act.defender_rolls[i] > 6)) {
                  reason = "defender roll out of range [1, 6]";
                  return false;
                }
                if (!used && act.defender_rolls[i] != 0) {
                  reason = "unused defender roll slots must be 0";
                  return false;
                }
              }
              return true;
            },
            [&](const FortifyAction &act) -> bool {
              if (m_initial_placement) {
                reason = "still in initial placement phase";
                return false;
              }
              if (queued_attack.has_value() || queued_defense.has_value()) {
                reason = "a queued battle must be resolved first";
                return false;
              }
              if (m_current_player < 0 ||
                  m_current_player >= static_cast<int>(NUM_PLAYERS)) {
                reason = "no player to move at this node";
                return false;
              }
              // num_units <= 1 means "skip fortify / end turn" and is always
              // legal (see FortifyToMoveUnits).
              if (act.num_units <= 1) {
                return true;
              }
              if (act.source < 0 ||
                  act.source >= static_cast<int>(m_map.size()) ||
                  act.target < 0 ||
                  act.target >= static_cast<int>(m_map.size())) {
                reason = "fortify source/target out of range";
                return false;
              }
              if (m_map[act.source].owner != m_current_player ||
                  m_map[act.target].owner != m_current_player) {
                reason = "can only fortify between own territories";
                return false;
              }
              if (act.source == act.target) {
                // No-op move the engine accepts (FortifyToMoveUnits subtracts
                // then re-adds); sample_action emits it when the player owns
                // a single territory. The engine only requires the units to
                // be present in this case.
                if (m_map[act.source].units <
                    static_cast<uint32_t>(act.num_units)) {
                  reason = "fortify moves more units than the source has";
                  return false;
                }
                return true;
              }
              if (m_map[act.source].units <
                  static_cast<uint32_t>(act.num_units) + 1) {
                reason = "fortify must leave at least one army behind";
                return false;
              }
              return true;
            },
        },
        action);
  }

  auto getConnectedComponent(size_t seed) -> std::bitset<kNumTerritories> {
    std::bitset<kNumTerritories> result{};
    std::array<size_t, kNumTerritories> stack;
    auto stack_ptr = stack.begin();

    auto push = [&](size_t elem) { *(stack_ptr++) = elem; };
    auto pop = [&](size_t elem) { return *(stack_ptr--); };

    result.set(seed);
    push(seed);

    while (stack_ptr != stack.begin()) {
      auto currnet_elem = pop();
      const CountryData &country_data = Board[currnet_elem];
      for (size_t i = 0; i < country_data.neighbor_count; ++i) {
        size_t neighbor = static_cast<size_t>(country_data.neighbors[i]);
        if (!result.test(neighbor) &&
            m_map[currnet_elem].owner == m_map[neighbor].owner) {
          result.set(neighbor);
          push(neighbor);
        }
      }
    }
    return result;
  }
};

template <size_t NUM_PLAYERS>
auto RenderRiskState(const RiskState<NUM_PLAYERS> &state,
                     std::span<const bool> highlighted) -> std::string {
  const int current_player = static_cast<int>(state.m_current_player);
  std::ostringstream os;
  os << "Turn: " << state.turn_count
     << " Player: " << PlayerColor(current_player) << current_player
     << kColorReset << "\n";
  os << "Reserves: ";
  for (size_t p = 0; p < NUM_PLAYERS; ++p)
    os << PlayerColor(static_cast<int>(p)) << "P" << p << kColorReset << ":"
       << state.m_reserves[p] << " ";
  os << "\n";
  os << "Units: ";
  std::array<int, NUM_PLAYERS> total_units{};
  for (const Territory &t : state.m_map) {
    if (t.owner >= 0) {
      total_units[static_cast<size_t>(t.owner)] += t.units;
    }
  }
  for (size_t p = 0; p < NUM_PLAYERS; ++p)
    os << PlayerColor(static_cast<int>(p)) << "P" << p << kColorReset << ":"
       << total_units[p] << " ";
  os << "\n";

  static constexpr int kBoardWidth = 80;
  static constexpr int kTextWidth = 3;
  static const auto segments = GetAsciiBoardTemplate(kBoardWidth, kTextWidth);
  if (segments.empty()) {
    for (size_t i = 0; i < state.m_map.size(); ++i) {
      if (const int owner = static_cast<int>(state.m_map[i].owner);
          owner != -1) {
        os << "  T" << i << ": " << PlayerColor(owner) << "P" << owner
           << kColorReset << " (" << state.m_map[i].units << ")\n";
      }
    }
    return os.str();
  }

  std::array<uint8_t, 43> colors{};
  colors[0] = 4;  // background = blue
  std::array<int, 43> troop_counts{};

  const bool highlight = highlighted.size() == state.m_map.size();
  for (size_t i = 0; i < state.m_map.size(); ++i) {
    uint8_t tid = kCountryToTerritoryId[i];
    uint8_t color =
        state.m_map[i].owner >= 0
            ? kPlayerColors[static_cast<size_t>(state.m_map[i].owner) %
                            kPlayerColors.size()]
            : 0;  // unowned = black
    // Bright variant highlights the territory regardless of owner.
    if (highlight && highlighted[i]) {
      color += 8;
    }
    colors[tid] = color;
    troop_counts[tid] = state.m_map[i].units;
  }

  os << RenderAsciiBoard(segments, colors, troop_counts) << "\n";
  return os.str();
}

static_assert(mcts::ChanceGame<risk_game::RiskState<2>>);
// mcts::PlayoutStep detects and prefers the in-place transition over the
// copying apply_action(), which matters in the rollout hot loop.
static_assert(mcts::InPlaceGame<risk_game::RiskState<2>>);

}  // namespace risk_game

#endif  // RISK_GAME_AI_CPP_RISK_RISK_GAME_H
