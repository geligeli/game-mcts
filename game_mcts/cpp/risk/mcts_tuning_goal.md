# Goal: MCTS tuning for 2-player Risk

Agreed goal text (drafted with write-goal, pending start). Start with a 4-hour
wall-clock budget via `CreateGoal` + `SetGoalBudget(4, hours)`.

---

Find a well-tuned MCTS configuration for 2-player Risk and prove it beats the default.

End state: a tuned MCTS configuration (flags and/or small code tweaks) that wins
>=60% of games against the default config in a final validation.

Done when: `bazel run -c opt //game_mcts/cpp/risk:risk_tournament` with a fixed seed and at
least 200 games shows the tuned config at >=60% win rate vs the default
(iterations=400, widening_c=2.0, widening_alpha=0.5, rollout=shortcut), and the
final config + standings are saved to `cpp/risk/tuning_result.md`.

Fairness rule: head-to-head comparisons run at equal per-move compute (same
`iterations` on both sides, 400 unless a change makes iterations cheaper) — the
tuning should be about search quality, not just more compute.

Loop: build with `-c opt`; evaluate candidates with small seeded tournaments
(e.g. 20-40 games/pair); promote winners into a short playoff; rerun
`bazel test //game_mcts/cpp/risk:all @game_mcts//...` after any code change.

Scope: only `cpp/risk/` in this repo (framework tweaks belong in the
game-mcts repo, e.g. exposing the UCB exploration constant in `mcts.inl`,
adjusting `RiskProposer` biases). No git commits, no changes elsewhere.

Stop rule: if the 4-hour budget runs out before reaching 60%, or builds/tests
stay broken, stop and report the best config found with its measured win rate
and the tournament logs as evidence — do not claim success without the numbers.

Tooling note: prefer the project MCP servers where connected (bazel MCP for
build/test, risk-engine MCP for diagnosing single positions via the MCTS
visit-count policy, clangd MCP for navigating the C++ edits); fall back to the
bazel CLI otherwise. Bulk tournament evaluation always runs through
`bazel run //game_mcts/cpp/risk:risk_tournament`.
