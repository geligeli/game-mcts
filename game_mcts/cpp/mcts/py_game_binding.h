#ifndef GAME_MCTS_GAME_MCTS_CPP_MCTS_PY_GAME_BINDING_H
#define GAME_MCTS_GAME_MCTS_CPP_MCTS_PY_GAME_BINDING_H

#include <pybind11/pybind11.h>

#include <string>

#include "game_mcts/cpp/mcts/py_game.h"

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
      .def("state_proto",
           [](const PyGame &game) {
             return pybind11::bytes(game.state_proto());
           })
      .def("state_proto_type", &PyGame::state_proto_type)
      .def("action_proto_type", &PyGame::action_proto_type)
      // pybind11's std::string caster accepts Python bytes verbatim, so the
      // caller passes serialized protos as bytes.
      .def("apply_action_proto",
           [](PyGame &game, const std::string &action) {
             game.apply_action_proto(action);
           })
      .def("check_action_proto",
           [](const PyGame &game, const std::string &action) {
             return game.check_action_proto(action);
           })
      .def("current_player", &PyGame::current_player)
      .def("num_players", &PyGame::num_players)
      .def("is_chance_node", &PyGame::is_chance_node)
      .def("sample_chance_action_proto",
           [](PyGame &game) {
             return pybind11::bytes(game.sample_chance_action_proto());
           })
      .def("is_terminal", &PyGame::is_terminal)
      .def("result", &PyGame::result)
      .def("winning_player", &PyGame::winning_player);
}

}  // namespace mcts

#endif  // GAME_MCTS_GAME_MCTS_CPP_MCTS_PY_GAME_BINDING_H
