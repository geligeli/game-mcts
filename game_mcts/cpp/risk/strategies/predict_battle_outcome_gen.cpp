#include <cmath>
#include <iomanip>
#include <iostream>
#include <sstream>

#include "cpp/risk/strategies/predict_battle_outcome.h"

using namespace risk_game;

int main() {
  // Cover battle sizes commonly reached in rollouts; larger battles fall back
  // to ComputeExpectedRemnants at runtime.
  int max_attackers = 64;
  int max_defenders = 64;

  std::ostringstream remaining_attackers_table;
  std::ostringstream remaining_defenders_table;

  const auto kType = "uint8_t";

  remaining_attackers_table << "static constexpr std::array<std::array<"
                            << kType << ", " << (max_defenders) << ">, "
                            << (max_attackers)
                            << "> kExpectedRemainingAttackers = {\n";
  remaining_defenders_table << "static constexpr std::array<std::array<"
                            << kType << ", " << (max_defenders) << ">, "
                            << (max_attackers)
                            << "> kExpectedRemainingDefenders = {\n";

  for (int initial_defenders = 1; initial_defenders <= max_defenders;
       ++initial_defenders) {
    remaining_attackers_table << "std::array<" << kType << ", " << max_attackers
                              << ">{";
    remaining_defenders_table << "std::array<" << kType << ", " << max_attackers
                              << ">{";

    for (int initial_attackers = 1; initial_attackers <= max_attackers;
         ++initial_attackers) {
      BattleRemnants r =
          ComputeExpectedRemnants(initial_attackers, initial_defenders);
      remaining_attackers_table << std::setw(2) << r.attackers << ", ";
      remaining_defenders_table << std::setw(2) << r.defenders << ", ";
    }
    remaining_attackers_table << "},\n";
    remaining_defenders_table << "},\n";
  }

  remaining_attackers_table << "};\n";
  remaining_defenders_table << "};\n";

  std::cout << "#include <array>\n";
  std::cout << "#include <cstdint>\n";
  std::cout << "#include \"cpp/risk/strategies/expected_battle_outcomes.h\"\n";
  std::cout << "namespace risk_game {\n";
  std::cout << remaining_attackers_table.str() << std::endl;
  std::cout << remaining_defenders_table.str() << std::endl;
  std::cout
      << "BattleRemnants LookupExpectedRemnants(int attackers, int defenders) "
         "{\n"
      << "  if (attackers >= 1 && attackers <= " << max_attackers
      << " && defenders >= 1 && defenders <= " << max_defenders << ") {\n"
      << "    return {kExpectedRemainingAttackers[attackers-1][defenders-1],\n"
      << "            kExpectedRemainingDefenders[attackers-1][defenders-1]};\n"
      << "  }\n"
      << "  return ComputeExpectedRemnants(attackers, defenders);\n"
      << "}\n";
  std::cout << "}  // namespace risk_game\n";
  return 0;
}