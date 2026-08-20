#ifndef GAME_MCTS_GAME_MCTS_CPP_MCTS_PY_MCTS_BINDING_H
#define GAME_MCTS_GAME_MCTS_CPP_MCTS_PY_MCTS_BINDING_H

#include <pybind11/functional.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <string>
#include <utility>
#include <vector>

#include "game_mcts/cpp/mcts/py_mcts.h"

namespace mcts {

// Binds the PyMcts interface under |name| in module |m|. Proto messages cross
// as Python bytes (see BindPyGameClass). Call once per module, then add
// game-specific factory functions returning std::unique_ptr<PyMcts>. No-op if
// |name| is already bound in |m|.
inline void BindPyMctsClass(pybind11::module_ &m,
                            const char *name = "MctsSearch") {
  if (pybind11::hasattr(m, name)) {
    return;
  }
  pybind11::class_<PyMcts>(m, name)
      .def("step", &PyMcts::step)
      .def("run", &PyMcts::run, pybind11::arg("iterations"))
      .def("best_action_proto",
           [](const PyMcts &search) {
             return pybind11::bytes(search.best_action_proto());
           })
      .def("root_policy",
           [](const PyMcts &search) {
             pybind11::list policy;
             for (const auto &[action_proto, visits, total_value] :
                  search.root_policy()) {
               policy.append(pybind11::make_tuple(pybind11::bytes(action_proto),
                                                  visits, total_value));
             }
             return policy;
           })
      .def("export_tree",
           [](const PyMcts &search) {
             return pybind11::bytes(search.export_tree());
           })
      .def("num_nodes", &PyMcts::num_nodes)
      .def("state_proto_type", &PyMcts::state_proto_type)
      .def("action_proto_type", &PyMcts::action_proto_type)
      .def(
          "set_observer",
          [](PyMcts &search, const pybind11::object &callback) {
            if (callback.is_none()) {
              search.set_observer({});
            } else {
              search.set_observer(
                  callback.cast<PyMcts::iteration_callback_t>());
            }
          },
          pybind11::arg("callback"),
          "callable(path: list[int], value: list[float]) per iteration, "
          "or None to disable");
}

}  // namespace mcts

#endif  // GAME_MCTS_GAME_MCTS_CPP_MCTS_PY_MCTS_BINDING_H
