#include "cpp/risk/risk_game.h"

#include <algorithm>
#include <cstring>  // for memcmp
#include <iostream>

namespace risk_game {

auto operator<<(std::ostream &os, const RiskAction &action) -> std::ostream & {
  os << std::visit(
      overloaded{
          [](const InitialPlaceAction &a) {
            return std::format("InitialPlace{{{}}}", Board[a.territory].name);
          },
          [](const PlayerAction &a) {
            std::ostringstream ss;
            ss << "PlayerAction{";
            if (a.reinforce_action) {
              for (size_t i = 0; i < a.reinforce_action->units_to_place.size();
                   ++i) {
                if (a.reinforce_action->units_to_place[i] > 0) {
                  ss << Board[i].name << "+"
                     << a.reinforce_action->units_to_place[i] << " ";
                }
              }
            }
            if (a.attack_action) {
              ss << "A:" << Board[a.attack_action->source].name << "->"
                 << Board[a.attack_action->target].name;
            }
            ss << "}";
            return std::move(ss).str();
          },
          [](const QueueDefenseAction &a) {
            return std::format("Defense{{{}}}", a.num_defend_dice);
          },
          [](const FortifyAction &a) {
            return std::format("Fortify{{{}->{} ({})}}", Board[a.source].name,
                               Board[a.target].name, a.num_units);
          },
          [](const RollDiceAction &a) {
            return std::format("RollDice{{A:[{},{},{}] D:[{},{}]}}",
                               a.attacker_rolls[0], a.attacker_rolls[1],
                               a.attacker_rolls[2], a.defender_rolls[0],
                               a.defender_rolls[1]);
          }},
      action);
  return os;
}

// RiskGame::RiskGame() {}

// RiskState RiskGame::get_initial_state() const {
//   RiskState s;
//   // Setup: Player 0 owns Node 0, Player 1 owns Node 1 & 2
//   s.map[0] = {0, 5};
//   s.map[1] = {1, 2};
//   s.map[2] = {1, 2};
//   s.current_player = 0;
//   s.phase = Phase::REINFORCE;
//   s.turn_count = 0;
//   return s;
// }

// int RiskGame::get_current_player(const RiskState &state) const {
//   return state.current_player;
// }

// // ACTION SPACE MAPPING:
// // 0..2: Place unit on Node X (Reinforce Phase)
// // 3..8: Attack from X to Y (Attack Phase) [Logic: 3 + source*2 +
// (target_idx)]
// // 9: PASS (End Phase)
// std::vector<int> RiskGame::get_legal_actions(const RiskState &s) const {
//   std::vector<int> actions;

//   if (s.phase == Phase::REINFORCE) {
//     // Can only reinforce territories you own
//     for (int i = 0; i < NUM_TERRITORIES; ++i) {
//       if (s.map[i].owner == s.current_player) {
//         actions.push_back(i);
//       }
//     }
//   } else if (s.phase == Phase::ATTACK) {
//     actions.push_back(9);  // Always allowed to stop attacking

//     // Attack logic: Must own source, >1 unit, neighbor is enemy
//     for (int src = 0; src < NUM_TERRITORIES; ++src) {
//       if (s.map[src].owner != s.current_player || s.map[src].units <= 1)
//         continue;

//       // Check neighbors (0-1, 1-2, 2-0 are connected)
//       int neighbors[] = {(src + 1) % 3, (src + 2) % 3};
//       for (int target : neighbors) {
//         if (s.map[target].owner != s.current_player) {
//           // Encode Action: simplified hash
//           actions.push_back(3 + src * 2 + (target > src ? 0 : 1));
//         }
//       }
//     }
//   } else if (s.phase == Phase::FORTIFY) {
//     actions.push_back(9);  // Just Pass for this simplified example
//   }
//   return actions;
// }

// std::tuple<RiskState, float, bool> RiskGame::step(const RiskState &s,
//                                                   int action) const {
//   RiskState next = s;
//   float reward = 0.0f;
//   bool done = false;

//   if (action == 9) {  // PASS / NEXT PHASE
//     if (next.phase == Phase::ATTACK)
//       next.phase = Phase::FORTIFY;
//     else if (next.phase == Phase::FORTIFY) {
//       next.phase = Phase::REINFORCE;
//       next.current_player = 1 - next.current_player;
//       next.turn_count++;
//     }
//   } else if (next.phase == Phase::REINFORCE) {
//     // Action is territory ID
//     next.map[action].units += 1;  // Add 1 unit
//     next.phase =
//         Phase::ATTACK;  // In this mini-game, 1 placement -> Attack phase
//   } else if (next.phase == Phase::ATTACK) {
//     // Decode action (very simplified collision logic)
//     // In real risk, you'd roll dice. Here: High unit count wins.
//     // We iterate to find which source/target this action corresponds to
//     // (Skipping full decode logic for brevity, assuming valid input)

//     // Deterministic Combat for RL testing:
//     // Attacker loses 1 unit.
//     // If Attacker has > Defender + 2, Attacker takes territory.
//     // (Just dummy logic to show state mutation)
//     for (int i = 0; i < NUM_TERRITORIES; ++i) {
//       if (s.map[i].owner == s.current_player && s.map[i].units > 1) {
//         next.map[i].units -= 1;  // Cost of war
//         break;
//       }
//     }
//   }

//   // Check Win Condition
//   int p0_count = 0;
//   for (const auto &t : next.map)
//     if (t.owner == 0) p0_count++;

//   if (p0_count == 3) {
//     reward = 1.0;
//     done = true;
//   }  // P1 Wins
//   if (p0_count == 0) {
//     reward = -1.0;
//     done = true;
//   }  // P2 Wins
//   if (next.turn_count > 100) done = true;  // Draw

//   return {next, reward, done};
// }

// std::vector<float> RiskGame::to_tensor(const RiskState &s) const {
//   std::vector<float> tensor;
//   for (const auto &t : s.map) {
//     tensor.push_back(static_cast<float>(t.owner));
//     tensor.push_back(static_cast<float>(t.units));
//   }
//   tensor.push_back(static_cast<float>(s.current_player));
//   tensor.push_back(static_cast<float>(s.phase));
//   return tensor;
// }

// std::string RiskGame::action_to_string(int action) const {
//   if (action == 9) return "PASS";
//   if (action < 3) return "REINFORCE Node " + std::to_string(action);
//   return "ATTACK Action " + std::to_string(action);
// }

}  // namespace risk_game