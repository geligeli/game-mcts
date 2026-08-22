# MCTS Tuning for 2-Player Risk — Results

Status: **final validation in progress** (200 games, seed 777). Screening complete; the
tuned config below won 81–89% of games across all screening matches.

## Objective

Find an MCTS configuration that wins ≥60% of games vs the default
(`iterations=400, widening_c=2.0, widening_alpha=0.5, rollout=shortcut`) at equal
per-move compute (400 iterations on both sides), validated over ≥200 games with a
fixed seed.

## Tuned configuration

```ini
[policy]
name = tuned
type = mcts
iterations = 400
widening_c = 2.0
widening_alpha = 0.5
proposer = stock
tree_proposer = smart
rollout = shortcut
```

The only difference from default is `tree_proposer = smart`: the in-tree move
proposer (a) places reserves only on border territories and (b) only proposes
attacks where `src.units - 1 > tgt.units` (end-turn is proposed with probability
`1/(num_favorable+1)`, as in the stock proposer). Rollouts stay `shortcut` with the
stock proposer — favorable-only attack proposers in rollouts were tried and stall
games: with both sides attacking only at a strict advantage, borders reach parity
and both players pass turn after turn without eliminating anyone, so playouts run
to the move cap (~18 min CPU per match with zero finished games). The stock
rollout's indiscriminate attacks are what keep playouts short and decisive.

## Code changes (all under cpp/mcts/ and cpp/risk/, tests green)

- `cpp/mcts/mcts.h`, `cpp/mcts/mcts.inl`: added `exploration_c` (default 1.0) to
  `compute_ucb` and both node pickers; exposed as INI key `exploration_c` in
  `risk_tournament.cpp`.
- `cpp/mcts/tournament.h`: streams `GAME_DONE task=<i> total=<n> a=<ia> b=<ib>
  score_a=<s>` per finished game (mutex-guarded, flushed) so long tournaments can
  be monitored live.
- `cpp/risk/risk_proposer.h`: `RiskProposer<NUM_PLAYERS, BORDER_REINFORCE,
  ATTACK_FAVORABLE>` template flags; `support_size()` mirrors the move filter.
- `cpp/risk/risk_tournament.cpp`: `MakeMctsPolicySplit<TREE_PROPOSER,
  ROLLOUT_PROPOSER>` allowing tree and rollout proposers to differ. New INI keys:
  `proposer` (stock|border), `tree_proposer` (stock|border|atk|smart).

`bazel test //game_mcts/cpp/risk:all //game_mcts/cpp/mcts:all` — all 17 tests passing (force-rerun,
not cached) after each change.

## Screening results (vs default, equal 400 iterations, 16 games each, seed 100)

| Candidate | Change | W | L | Win rate |
|---|---|---|---|---|
| w4_a05 | widening_c=4 | 7 | 7 | 50% |
| ec05 | exploration_c=0.5 | 7 | 7 | 50% |
| w2_a075 | widening_alpha=0.75 | 6 | 9 | 40% |
| border | proposer=border (tree+rollout) | 6 | 9 | 40% |
| w1_a05 | widening_c=1 | 3 | 9 | 25% |
| w2_a025 | widening_alpha=0.25 | 1 | 3 | 25% |
| ec20 | exploration_c=2.0 | 4 | 12 | 25% |
| tatk | tree_proposer=atk | 5 | 2 | 71% (stopped early) |
| **tsmart** | **tree_proposer=smart** | **13** | **3** | **81%** |
| tsmart (seed 200) | same | 7 | 1 | 88% (stopped early) |

No draws occurred in any match. Pure search-knob variants all landed ≤50%, i.e.
the default is at a local optimum for those knobs; the win came from biasing the
in-tree action proposals.

Combined tsmart screening record: **20 W / 4 L (83%)** across seeds 100 and 200.

## Final validation

- Command:
  `bazel-bin/cpp/risk/risk_tournament --config=cpp/risk/tuning_logs/final_validation.cfg
  --games_per_pair=200 --seed=777 --threads=20
  --output=cpp/risk/tuning_logs/final_validation.csv`
- Config: `cpp/risk/tuning_logs/final_validation.cfg` (default index 0, tuned index 1).
- An earlier 16-thread attempt was stopped for core reallocation after 18 games:
  tuned **16 W / 2 L (89%)** (`final_validation_16t_partial.log`).
- The 20-thread run over the full 200 games is currently running; standings will
  be filled in here when it completes. Logs live in `cpp/risk/tuning_logs/`
  (`final_validation.log` / `.csv`).

### Final standings (200 games, seed 777)

_Pending._
