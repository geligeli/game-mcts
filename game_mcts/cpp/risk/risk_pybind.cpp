// Python bindings for driving Risk games from serialized state/action protos.
// The game-agnostic interface (mcts::PyGame) lives in the game-mcts repo
// (game_mcts/cpp/mcts/py_game.h); this module only adds the Risk factory,
// dispatching over the player-count template arity. Example:
//   game = risk_engine.new_game(num_players=3, seed=1)
//   game.apply_action_proto(risk_pb2.RiskAction(...).SerializeToString())

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <random>
#include <stdexcept>
#include <string>

#include "cpp/risk/risk.pb.h"
#include "cpp/risk/risk_game.h"
#include "cpp/risk/risk_serialization.h"
#include "cpp/risk/strategies/risk_proposer.h"
#include "cpp/risk/strategies/risk_rollout_shortcuts.h"
#include "game_mcts/cpp/mcts/py_game.h"
#include "game_mcts/cpp/mcts/py_game_binding.h"
#include "game_mcts/cpp/mcts/py_mcts.h"
#include "game_mcts/cpp/mcts/py_mcts_binding.h"

namespace {

template <std::size_t NUM_PLAYERS>
auto MakeRiskPyGame(const risk_game::proto::RiskState *state,
                    std::uint32_t seed) -> std::unique_ptr<mcts::PyGame> {
  using game_t = risk_game::RiskState<NUM_PLAYERS>;
  game_t initial;
  if (state != nullptr) {
    // Validates arity, territory count and per-field ranges; CHECK-fails on
    // malformed input (see risk_serialization.cpp).
    initial = mcts::GameSerializationTraits<game_t>::StateFromProto(*state);
  }
  return mcts::MakePyGame<game_t>(std::move(initial), seed);
}

auto NewGame(int num_players, const std::optional<std::string> &state_proto,
             std::optional<std::uint32_t> seed)
    -> std::unique_ptr<mcts::PyGame> {
  risk_game::proto::RiskState parsed;
  const risk_game::proto::RiskState *state = nullptr;
  if (state_proto.has_value()) {
    if (!parsed.ParseFromString(*state_proto)) {
      throw std::invalid_argument("cannot parse risk_game.proto.RiskState");
    }
    // The recorded arity wins over the argument.
    num_players = static_cast<int>(parsed.num_players());
    state = &parsed;
  }
  const std::uint32_t actual_seed = seed.value_or(std::random_device{}());
  switch (num_players) {
    case 2:
      return MakeRiskPyGame<2>(state, actual_seed);
    case 3:
      return MakeRiskPyGame<3>(state, actual_seed);
    case 4:
      return MakeRiskPyGame<4>(state, actual_seed);
    case 5:
      return MakeRiskPyGame<5>(state, actual_seed);
    case 6:
      return MakeRiskPyGame<6>(state, actual_seed);
    default:
      throw std::invalid_argument("num_players must be in 2..6, got " +
                                  std::to_string(num_players));
  }
}

template <std::size_t NUM_PLAYERS>
auto MakeRiskPyMcts(const risk_game::proto::RiskState *state,
                    bool exact_rollouts, double widening_c,
                    double widening_alpha,
                    std::uint32_t seed) -> std::unique_ptr<mcts::PyMcts> {
  using game_t = risk_game::RiskState<NUM_PLAYERS>;
  using proposer_t = risk_game::RiskProposer<NUM_PLAYERS>;
  using traits_t = mcts::GameSerializationTraits<game_t>;
  game_t root;
  if (state != nullptr) {
    root = traits_t::StateFromProto(*state);
  }
  if (exact_rollouts) {
    return mcts::MakePyMcts<game_t>(std::move(root), proposer_t{},
                                    mcts::RandomRollout<game_t, proposer_t>{},
                                    widening_c, widening_alpha, seed);
  }
  return mcts::MakePyMcts<game_t>(
      std::move(root), proposer_t{},
      mcts::MakeShortcutRollout<game_t, proposer_t>(
          &risk_game::ResolveBattleWithExpectationInPlace<NUM_PLAYERS>),
      widening_c, widening_alpha, seed);
}

auto NewMcts(int num_players, const std::optional<std::string> &state_proto,
             const std::string &rollout, double widening_c,
             double widening_alpha, std::optional<std::uint32_t> seed)
    -> std::unique_ptr<mcts::PyMcts> {
  bool exact_rollouts;
  if (rollout == "exact") {
    exact_rollouts = true;
  } else if (rollout == "expected") {
    exact_rollouts = false;
  } else {
    throw std::invalid_argument("rollout must be 'exact' or 'expected', got '" +
                                rollout + "'");
  }
  risk_game::proto::RiskState parsed;
  const risk_game::proto::RiskState *state = nullptr;
  if (state_proto.has_value()) {
    if (!parsed.ParseFromString(*state_proto)) {
      throw std::invalid_argument("cannot parse risk_game.proto.RiskState");
    }
    num_players = static_cast<int>(parsed.num_players());
    state = &parsed;
  }
  const std::uint32_t actual_seed = seed.value_or(std::random_device{}());
  switch (num_players) {
    case 2:
      return MakeRiskPyMcts<2>(state, exact_rollouts, widening_c,
                               widening_alpha, actual_seed);
    case 3:
      return MakeRiskPyMcts<3>(state, exact_rollouts, widening_c,
                               widening_alpha, actual_seed);
    case 4:
      return MakeRiskPyMcts<4>(state, exact_rollouts, widening_c,
                               widening_alpha, actual_seed);
    case 5:
      return MakeRiskPyMcts<5>(state, exact_rollouts, widening_c,
                               widening_alpha, actual_seed);
    case 6:
      return MakeRiskPyMcts<6>(state, exact_rollouts, widening_c,
                               widening_alpha, actual_seed);
    default:
      throw std::invalid_argument("num_players must be in 2..6, got " +
                                  std::to_string(num_players));
  }
}

}  // namespace

PYBIND11_MODULE(risk_engine, m) {
  m.doc() =
      "Drives Risk games from serialized state/action protos "
      "(risk_game.proto.RiskState / RiskAction). Fully replayable from "
      "recorded trajectories; chance nodes (dice) can also be sampled.";
  mcts::BindPyGameClass(m, "Game");
  m.def("new_game", &NewGame, pybind11::arg("num_players") = 2,
        pybind11::arg("state_proto") = pybind11::none(),
        pybind11::arg("seed") = pybind11::none(),
        "New game as a Game. With state_proto (serialized "
        "risk_game.proto.RiskState), num_players is taken from the proto.");
  mcts::BindPyMctsClass(m, "MctsSearch");
  m.def("new_mcts", &NewMcts, pybind11::arg("num_players") = 2,
        pybind11::arg("state_proto") = pybind11::none(),
        pybind11::arg("rollout") = "expected",
        pybind11::arg("widening_c") = 2.0,
        pybind11::arg("widening_alpha") = 0.5,
        pybind11::arg("seed") = pybind11::none(),
        "New MCTS search (RiskProposer tree policy; rollout='exact' | "
        "'expected') rooted at state_proto (default: initial state).");
}
