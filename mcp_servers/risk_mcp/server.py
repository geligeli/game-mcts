"""Risk engine MCP server: drive Risk games and MCTS searches.

Wraps the risk_engine pybind module (//game_mcts/cpp/risk:risk_engine, built
by bazel)
which exposes mcts::PyGame (rules-only driving via serialized protos) and
mcts::PyMcts (MCTS with RiskProposer, exact or expected-outcome rollouts).

Typical workflows:
- Verify a rules change: risk_new_game -> risk_apply_action* -> risk_describe.
- Inspect a dumped position: risk_new_game(state_text=<proto-text dump from
  the self-play binary's 's' key>) -> risk_mcts_policy.
- "What does MCTS play here?": risk_play_mcts_move.

Actions are given as proto-text of risk_game.proto.RiskAction, e.g.:
  initial_place { territory: 17 }
  player_action { attack_action { source: 30 target: 34 num_attack_dice: 3 } }
  queue_defense { num_defend_dice: 2 }
  fortify { source: 1 target: 2 num_units: 4 }

Run with the shared venv:  mcp_servers/.venv/bin/python server.py
Requires: bazel build //game_mcts/cpp/risk:risk_engine \
              //game_mcts/cpp/risk:risk_py_proto
"""

import itertools
import os
import re
import sys
from pathlib import Path

from mcp.server.fastmcp import FastMCP

REPO_ROOT = Path(
    os.environ.get("RISK_MCP_REPO_ROOT", Path(__file__).resolve().parents[2])
)
for rel in ("game_mcts/cpp/risk", "game_mcts/cpp/mcts"):
    sys.path.insert(0, str(REPO_ROOT / "bazel-bin" / rel))

try:
    import risk_engine
    import risk_pb2
    from google.protobuf import text_format
except ImportError as e:  # pragma: no cover - environment setup issue
    raise SystemExit(
        f"{e}\nArtifacts missing? Run:\n"
        "  bazel build //game_mcts/cpp/risk:risk_engine"
        " //game_mcts/cpp/risk:risk_py_proto"
    )

MAX_MCTS_ITERATIONS = 200_000

# ---------------------------------------------------------------------------
# Territory names, parsed from game_mcts/cpp/risk/risk_board.h (single source
# of truth).
# Territory index i in RiskState == i-th entry of the Country enum.
# ---------------------------------------------------------------------------


def _load_territory_names() -> list[str]:
    header = (REPO_ROOT / "game_mcts/cpp/risk/risk_board.h").read_text()
    # Enum order defines the territory index.
    enum_block = re.search(
        r"enum class Country[^{]*\{(.*?)\}", header, re.DOTALL
    ).group(1)
    enum_names = [
        m.strip()
        for m in enum_block.split(",")
        if m.strip() and m.strip() != "COUNT"
    ]
    # Pretty display names from the CountryData table.
    display = dict(re.findall(r'\{Country::(\w+),\s*"([^"]+)"', header))
    return [display.get(name, name.replace("_", " ")) for name in enum_names]


TERRITORY_NAMES = _load_territory_names()

mcp = FastMCP("risk-engine")

_games: dict[str, object] = {}  # game_id -> risk_engine.Game
_id_counter = itertools.count(1)


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------


def _get_game(game_id: str):
    if game_id not in _games:
        raise KeyError(
            f"unknown game_id '{game_id}'; known: {sorted(_games) or '(none)'}"
        )
    return _games[game_id]


def _parse_action(action_text: str) -> risk_pb2.RiskAction:
    action = risk_pb2.RiskAction()
    text_format.Parse(action_text, action)
    if action.WhichOneof("action") is None:
        raise ValueError(f"empty RiskAction (parsed from: {action_text!r})")
    return action


def _parse_state(state_text: str) -> bytes:
    state = risk_pb2.RiskState()
    text_format.Parse(state_text, state)
    return state.SerializeToString()


def _compact_action(action: risk_pb2.RiskAction) -> str:
    """One-line action rendering with territory names instead of indices."""
    kind = action.WhichOneof("action")
    name = lambda i: TERRITORY_NAMES[i] if 0 <= i < len(TERRITORY_NAMES) else f"#{i}"
    if kind == "initial_place":
        return f"place {name(action.initial_place.territory)}"
    if kind == "player_action":
        pa = action.player_action
        parts = []
        if pa.HasField("reinforce_action"):
            placed = [
                f"{name(i)}+{u}"
                for i, u in enumerate(pa.reinforce_action.units_to_place)
                if u > 0
            ]
            parts.append("reinforce {" + ", ".join(placed) + "}")
        if pa.HasField("attack_action"):
            a = pa.attack_action
            parts.append(
                f"attack {name(a.source)} -> {name(a.target)} with {a.num_attack_dice} dice"
            )
        return "; ".join(parts) if parts else "pass (no reinforce, no attack)"
    if kind == "queue_defense":
        return f"defend with {action.queue_defense.num_defend_dice} dice"
    if kind == "fortify":
        f = action.fortify
        return f"fortify {name(f.source)} -> {name(f.target)} x{f.num_units}"
    if kind == "roll_dice":
        r = action.roll_dice
        return f"dice att{list(r.attacker_rolls)} def{list(r.defender_rolls)}"
    return text_format.MessageToString(action, as_one_line=True)


def _status(game) -> str:
    if game.is_terminal():
        result = game.result()
        if result == 2:
            return f"GAME OVER: player {game.winning_player()} wins"
        return "GAME OVER: draw"
    if game.is_chance_node():
        return "at chance node (dice) — call risk_roll_chance"
    return f"player {game.current_player()} to move"


def _describe(game) -> str:
    state = risk_pb2.RiskState.FromString(game.state_proto())
    lines = [_status(game)]
    lines.append(
        f"turn {state.turn_count}, players {state.num_players}, "
        f"reserves: {list(state.reserves)}"
    )
    phase = []
    if state.initial_placement:
        phase.append(f"initial placement ({state.num_initial_placements} done)")
    if state.HasField("queued_attack"):
        a = state.queued_attack
        phase.append(
            f"attack queued: {TERRITORY_NAMES[a.source]} -> "
            f"{TERRITORY_NAMES[a.target]} ({a.num_attack_dice} dice)"
        )
    if state.HasField("queued_defense"):
        phase.append("defense queued")
    if phase:
        lines.append("phase: " + "; ".join(phase))
    by_owner: dict[int, list[str]] = {}
    for i, t in enumerate(state.territories):
        by_owner.setdefault(t.owner, []).append(f"{TERRITORY_NAMES[i]} ({t.units})")
    for owner in sorted(by_owner):
        label = f"player {owner}" if owner >= 0 else "unowned"
        territories = by_owner[owner]
        lines.append(f"{label} [{len(territories)}]: " + ", ".join(territories))
    return "\n".join(lines)


def _run_search(
    state_proto: bytes,
    iterations: int,
    rollout: str,
    widening_c: float,
    widening_alpha: float,
    seed: int | None,
):
    if not 1 <= iterations <= MAX_MCTS_ITERATIONS:
        raise ValueError(f"iterations must be in 1..{MAX_MCTS_ITERATIONS}")
    search = risk_engine.new_mcts(
        state_proto=state_proto,
        rollout=rollout,
        widening_c=widening_c,
        widening_alpha=widening_alpha,
        seed=seed,
    )
    search.run(iterations)
    return search


def _format_policy(search, iterations: int, top_k: int) -> str:
    policy = sorted(search.root_policy(), key=lambda e: -e[1])
    lines = [
        f"root policy after {iterations} iterations "
        f"({search.num_nodes()} nodes), top {min(top_k, len(policy))} "
        f"of {len(policy)}:"
    ]
    for action_bytes, visits, total_value in policy[:top_k]:
        action = risk_pb2.RiskAction.FromString(action_bytes)
        means = ", ".join(f"p{p}={v / visits:+.3f}" for p, v in enumerate(total_value))
        lines.append(
            f"  {visits:>6} visits ({100.0 * visits / iterations:5.1f}%) "
            f"mean value [{means}]  {_compact_action(action)}"
        )
    best = risk_pb2.RiskAction.FromString(search.best_action_proto())
    lines.append(f"best action: {_compact_action(best)}")
    return "\n".join(lines)


# ---------------------------------------------------------------------------
# Tools
# ---------------------------------------------------------------------------


@mcp.tool()
def risk_new_game(
    num_players: int = 2, state_text: str = "", seed: int | None = None
) -> str:
    """Starts a Risk game session and returns its game_id.

    state_text: optional proto-text risk_game.proto.RiskState to start from
    (e.g. a dump from the self-play binary's 's' key); num_players is then
    taken from the proto. seed fixes the chance-node RNG (dice).
    """
    state_proto = _parse_state(state_text) if state_text.strip() else None
    game = risk_engine.new_game(
        num_players=num_players, state_proto=state_proto, seed=seed
    )
    game_id = f"g{next(_id_counter)}"
    _games[game_id] = game
    return f"game_id: {game_id}\n{_status(game)}"


@mcp.tool()
def risk_describe(game_id: str) -> str:
    """Human-readable board: whose turn, phase, reserves, and per-player
    territory lists with unit counts."""
    return _describe(_get_game(game_id))


@mcp.tool()
def risk_state_proto(game_id: str) -> str:
    """Exact proto-text of the current RiskState — use for diffing, saving
    positions, or feeding back into risk_new_game(state_text=...)."""
    state = risk_pb2.RiskState.FromString(_get_game(game_id).state_proto())
    return text_format.MessageToString(state)


@mcp.tool()
def risk_apply_action(game_id: str, action_text: str) -> str:
    """Applies a proto-text RiskAction (see module docstring for examples).
    Fails with the referee's reason if the action is illegal. At chance nodes
    use risk_roll_chance instead — dice come from the rules, not the caller.
    """
    game = _get_game(game_id)
    action = _parse_action(action_text)
    game.apply_action_proto(action.SerializeToString())
    return f"applied: {_compact_action(action)}\n{_status(game)}"


@mcp.tool()
def risk_check_action(game_id: str, action_text: str) -> str:
    """Referee check without applying: 'legal' or the reason it is not."""
    game = _get_game(game_id)
    action = _parse_action(action_text)
    reason = game.check_action_proto(action.SerializeToString())
    return "legal" if reason == "" else f"illegal: {reason}"


@mcp.tool()
def risk_roll_chance(game_id: str) -> str:
    """Samples the rules-defined chance action (dice) and applies it."""
    game = _get_game(game_id)
    action_bytes = game.sample_chance_action_proto()
    game.apply_action_proto(action_bytes)
    action = risk_pb2.RiskAction.FromString(action_bytes)
    return f"chance: {_compact_action(action)}\n{_status(game)}"


@mcp.tool()
def risk_mcts_policy(
    game_id: str = "",
    state_text: str = "",
    iterations: int = 1000,
    rollout: str = "expected",
    widening_c: float = 2.0,
    widening_alpha: float = 0.5,
    seed: int | None = None,
    top_k: int = 10,
) -> str:
    """Runs MCTS from a position and returns the visit-count policy: per root
    action the visits and mean rollout values per player (+1 win / -1 loss).

    Root is game_id's current state or a proto-text RiskState (one of the
    two). rollout: 'expected' (fast, battle expectation table) or 'exact'.
    """
    if bool(game_id) == bool(state_text.strip()):
        raise ValueError("pass exactly one of game_id or state_text")
    state_proto = (
        _get_game(game_id).state_proto()
        if game_id
        else _parse_state(state_text)
    )
    search = _run_search(
        state_proto, iterations, rollout, widening_c, widening_alpha, seed
    )
    return _format_policy(search, iterations, top_k)


@mcp.tool()
def risk_play_mcts_move(
    game_id: str,
    iterations: int = 1000,
    rollout: str = "expected",
    widening_c: float = 2.0,
    widening_alpha: float = 0.5,
    seed: int | None = None,
    top_k: int = 5,
) -> str:
    """Computes the MCTS move for the current position and applies it to the
    game. At chance nodes it rolls the dice instead (no search at chance
    nodes — the rules decide)."""
    game = _get_game(game_id)
    if game.is_terminal():
        return _status(game)
    if game.is_chance_node():
        return risk_roll_chance(game_id)
    search = _run_search(
        game.state_proto(), iterations, rollout, widening_c, widening_alpha, seed
    )
    best = search.best_action_proto()
    action = risk_pb2.RiskAction.FromString(best)
    game.apply_action_proto(best)
    return (
        _format_policy(search, iterations, top_k)
        + f"\napplied: {_compact_action(action)}\n{_status(game)}"
    )


@mcp.tool()
def risk_list_games() -> str:
    """Lists all live game sessions with their status."""
    if not _games:
        return "(no games)"
    return "\n".join(f"{gid}: {_status(g)}" for gid, g in _games.items())


@mcp.tool()
def risk_close(game_id: str) -> str:
    """Drops a game session."""
    _get_game(game_id)
    del _games[game_id]
    return f"closed {game_id}"


if __name__ == "__main__":
    mcp.run()
