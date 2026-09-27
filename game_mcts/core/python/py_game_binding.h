#ifndef GAME_MCTS_GAME_MCTS_CORE_PYTHON_PY_GAME_BINDING_H
#define GAME_MCTS_GAME_MCTS_CORE_PYTHON_PY_GAME_BINDING_H

#include <pybind11/pybind11.h>

#include <string>

#include "game_mcts/core/python/py_game.h"

namespace mcts {

// Binds the PyGame interface under |name| in module |m|. Proto messages cross
// as Python bytes (serialized): returning a std::string directly would make
// pybind11 UTF-8-decode it, which arbitrary proto bytes do not survive. Call
// once per module, then add game-specific factory functions returning
// std::unique_ptr<PyGame>. No-op if |name| is already bound in |m|, so
// several games can share one module.
inline void BindPyGameClass(pybind11::module_ &m, const char *name = "Game") {
  if (pybind11::hasattr(m, name)) {
    return;
  }
  pybind11::class_<PyGame>(m, name)
      .def(
          "state_proto",
          [](const PyGame &game) { return pybind11::bytes(game.StateProto()); })
      .def("state_proto_type", &PyGame::StateProtoType)
      .def("action_proto_type", &PyGame::ActionProtoType)
      // pybind11's std::string caster accepts Python bytes verbatim, so the
      // caller passes serialized protos as bytes.
      .def("apply_action_proto",
           [](PyGame &game, const std::string &action) {
             game.ApplyActionProto(action);
           })
      .def("check_action_proto",
           [](const PyGame &game, const std::string &action) {
             return game.CheckActionProto(action);
           })
      .def("current_player", &PyGame::current_player)
      .def("num_players", &PyGame::NumPlayers)
      .def("is_chance_node", &PyGame::is_chance_node)
      .def("sample_chance_action_proto",
           [](PyGame &game) {
             return pybind11::bytes(game.SampleChanceActionProto());
           })
      .def("is_terminal", &PyGame::is_terminal)
      .def("result", &PyGame::Result)
      .def("winning_player", &PyGame::winning_player);
}

}  // namespace mcts

#endif  // GAME_MCTS_GAME_MCTS_CORE_PYTHON_PY_GAME_BINDING_H
