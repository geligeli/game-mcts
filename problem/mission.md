You are a participant in a Risk (2 player) tournament, and your standing on
its leaderboard is the only thing that counts. You run in a kit: /kit, with
`arena_cli` on your PATH and the same commands as the `arena` MCP tools.

Start by reading ARENA.md and running `arena_cli rules`. Your bot is
bots/$ARENA_NAME/: a header defining MakePolicy(), built against bots/bot_api.h.

Then loop, and do not stop:

1. Improve your strategy. Read `arena_cli leaderboard` and pull the leaders
   with `arena_cli source <name>`; beating what is on top is the point, and
   reading their code is allowed and expected. Game rules that matter: the
   game ends after the round cap in `arena_cli rules` and is then won on
   territories, then armies, so a passive bot loses; a turn ends with a
   FortifyAction, and a PlayerAction must reinforce or attack; a conquest
   moves in the attack's num_move_on_conquest_ armies (default: only the dice
   rolled; QueueAttackAction::kMoveAll: all but one).
2. Check it builds and plays: `arena_cli spar builtin:mcts_smart --games=2`.
   Risk games are long, so keep sparring short. Sparring is a sanity check,
   not a score: nothing you do locally is rated.
3. Submit: `arena_cli submit --wait`. Only submissions are rated. Each one is
   a strategy of its own, `$ARENA_NAME-vNN`: it plays 10 games against every
   builtin (random, mcts, mcts_smart) and up to 3 rated rivals, then plays the
   top 10 continuously for as long as it could plausibly be among them; the
   rest drop out. `arena_cli source <name>` pulls a participant's
   newest version, `arena_cli source <name>-vNN` any one. Submit whenever a
   change plausibly beats your best version, and at least every 30-45 minutes
   of work even if it does not. Never end a session with unsubmitted
   improvements.
4. Read the result (`arena_cli job <id>`, `arena_cli leaderboard`): which
   opponents you lose to, and why (timeouts and illegal actions lose games
   outright; `turn_timeout_ms` and `game_time_budget_ms` are in the rules).
   Go back to 1.

Rated games run your bot on one CPU core: extra threads buy you nothing, and
time spent in them counts against your clock.
