"""End-to-end test of the risk_engine pybind module: a Risk game is fully
drivable from serialized state/action protos (mcts::PyGame interface)."""

import unittest

import mcts_tree_pb2
import risk_engine
import risk_pb2

NUM_TERRITORIES = 42


def place(territory):
    return risk_pb2.RiskAction(
        initial_place=risk_pb2.InitialPlaceAction(territory=territory)
    ).SerializeToString()


class RiskEngineTest(unittest.TestCase):
    def test_initial_state(self):
        game = risk_engine.new_game(num_players=3, seed=1)
        self.assertEqual(game.num_players(), 3)
        self.assertEqual(game.current_player(), 0)
        self.assertFalse(game.is_chance_node())
        self.assertFalse(game.is_terminal())
        self.assertEqual(game.result(), 0)
        self.assertEqual(game.winning_player(), -1)
        self.assertEqual(game.state_proto_type(), "risk_game.proto.RiskState")
        self.assertEqual(game.action_proto_type(), "risk_game.proto.RiskAction")

        state = risk_pb2.RiskState.FromString(game.state_proto())
        self.assertEqual(state.num_players, 3)
        self.assertEqual(len(state.territories), NUM_TERRITORIES)
        self.assertTrue(all(t.owner == -1 for t in state.territories))

    def test_scripted_initial_placement(self):
        game = risk_engine.new_game(num_players=3, seed=1)
        for territory in range(NUM_TERRITORIES):
            self.assertEqual(game.current_player(), territory % 3)
            game.apply_action_proto(place(territory))

        state = risk_pb2.RiskState.FromString(game.state_proto())
        for territory in range(NUM_TERRITORIES):
            self.assertEqual(state.territories[territory].owner, territory % 3)
            self.assertEqual(state.territories[territory].units, 1)
        # Placement over: the game continues (reinforcements) with player 0.
        self.assertEqual(game.current_player(), 0)
        self.assertEqual(game.result(), 0)

    def test_state_roundtrip_via_new_game(self):
        game = risk_engine.new_game(num_players=3, seed=1)
        for territory in range(7):
            game.apply_action_proto(place(territory))
        clone = risk_engine.new_game(state_proto=game.state_proto())
        self.assertEqual(clone.num_players(), 3)
        self.assertEqual(clone.current_player(), game.current_player())
        self.assertEqual(clone.state_proto(), game.state_proto())

    def test_errors(self):
        game = risk_engine.new_game(num_players=2, seed=1)
        with self.assertRaises(ValueError):
            game.apply_action_proto(b"\xff\xfe")  # not a proto
        with self.assertRaises(ValueError):
            game.apply_action_proto(place(NUM_TERRITORIES))  # out of range
        self.assertIn("cannot parse", game.check_action_proto(b"\xff\xfe"))
        self.assertNotEqual(game.check_action_proto(place(NUM_TERRITORIES)), "")
        with self.assertRaises(ValueError):
            risk_engine.new_game(num_players=7)
        with self.assertRaises(ValueError):
            risk_engine.new_game(state_proto=b"\xff\xfe")


class RiskMctsTest(unittest.TestCase):
    def setUp(self):
        # A mid-game-ish root: initial placement fully done, 3 players.
        game = risk_engine.new_game(num_players=3, seed=1)
        for territory in range(NUM_TERRITORIES):
            game.apply_action_proto(place(territory))
        self.root_state = game.state_proto()

    def test_search_runs_and_policy_is_consistent(self):
        search = risk_engine.new_mcts(
            state_proto=self.root_state, rollout="expected", seed=1
        )
        self.assertEqual(search.num_nodes(), 1)
        self.assertEqual(search.state_proto_type(), "risk_game.proto.RiskState")

        search.run(50)
        self.assertGreater(search.num_nodes(), 1)

        policy = search.root_policy()
        self.assertTrue(policy)
        total_visits = 0
        for action_bytes, visits, total_value in policy:
            risk_pb2.RiskAction.FromString(action_bytes)  # must parse
            self.assertGreater(visits, 0)
            self.assertEqual(len(total_value), 3)
            total_visits += visits
        self.assertEqual(total_visits, 50)

        best = risk_pb2.RiskAction.FromString(search.best_action_proto())
        self.assertIsNotNone(best.WhichOneof("action"))

    def test_export_tree(self):
        search = risk_engine.new_mcts(
            state_proto=self.root_state, rollout="expected", seed=1
        )
        search.run(30)
        tree = mcts_tree_pb2.MctsTree.FromString(search.export_tree())
        self.assertEqual(len(tree.nodes), search.num_nodes())
        self.assertEqual(tree.nodes[0].num_visits, 30)
        for node in tree.nodes:
            for edge in node.edges:
                self.assertLess(edge.child_node_index, len(tree.nodes))
                risk_pb2.RiskAction.FromString(edge.action)  # must parse

    def test_observer(self):
        search = risk_engine.new_mcts(
            state_proto=self.root_state, rollout="expected", seed=1
        )
        calls = []
        search.set_observer(lambda path, value: calls.append((path, value)))
        search.run(10)
        self.assertEqual(len(calls), 10)
        for path, value in calls:
            self.assertEqual(path[0], 0)  # path starts at the root
            self.assertLess(len(path), search.num_nodes() + 1)
            self.assertEqual(len(value), 3)

        search.set_observer(None)  # off
        search.run(5)
        self.assertEqual(len(calls), 10)

    def test_errors(self):
        with self.assertRaises(ValueError):
            risk_engine.new_mcts(state_proto=self.root_state, rollout="bogus")
        with self.assertRaises(ValueError):
            risk_engine.new_mcts(state_proto=b"\xff\xfe")


if __name__ == "__main__":
    unittest.main()
