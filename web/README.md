# web/: risk3 in the browser

A 3D viewer for every game the arena stored, and a page to play any candidate
version, which takes both other seats of the three. The candidate's own
`strategy.h` is compiled to WebAssembly, and its threads come along as
pthreads. Nothing in the problem, the kits or the sandbox depends on this
package.

```sh
bash web/fetch_assets.sh                                # three.js + CC0 assets
python3 web/stage.py ~/.arena/risk3-swiss-<time>         # the candidates
bash web/build.sh                                        # ... to WebAssembly, in web/dist
python3 web/serve.py --data_dir ~/.arena/risk3-swiss-<time> --data_dir ~/.arena/risk3
```

Then open http://localhost:8095/. It is https://takumi.games/sessions/ from
outside, where nginx on geli-nfs proxies that path to this host's :8095.

## The pieces

- **`risk_match.{h,cc}`**: one game between a person and a policy in every
  other seat.
  - It covers the rules, dice, the round cap, and captions and views from
    `problem/risk_view.h` (the referee's own).
  - It answers JSON: the steps a call played, then where the game stands.
  - `risk_match_test` plays whole games natively.
- **`bot_wasm.cc` + `wasm.bzl`**: a candidate's header, compiled around the
  engine the way `bots/bot.cc` compiles it for the referee.
  - The output is an ES module factory, `createBot()`, whose `rk_*` exports
    `static/js/engine_worker.js` calls from a dedicated worker.
  - The pthread pool is made up front. A policy that blocks, for example on
    joining its search threads, blocks the worker, never the page.
- **`stage.py`**: copies the versions of a Swiss re-rank (`arena_tournament
  swiss`) and `bots/reference` into `web/bots/`. It writes the BUILD there
  and `manifest.json` (each version's Swiss rating). `web/bots/` is generated.
- **`build.sh`**: builds them (`--config=web`) and copies the modules to
  `web/dist/bots/`, since `bazel-bin` follows whatever was built last.
- **`selftest.html`**: plays one quick game against `?bot=` and reports in
  the title. That is how headless Chrome checks a module and its threads.
- **`serve.py`**: a read-only server.
  - It serves the pages, the built modules, the games' index and their raw
    `GameRecord`s.
  - Every response is cross-origin isolated (COOP + COEP). That is what allows
    `SharedArrayBuffer`, and so WebAssembly threads. Browsers also require a
    secure context: https, or localhost.
- **`static/`**:
  - `index.html`: the lobby.
  - `replay.html`: the viewer. It decodes a `GameRecord` in `js/record.js`.
  - `play.html`: the play page.
  - `js/risk3d.js`: the board the viewer and the play page share, drawn from
    `problem/risk_map.svg`. The viewer and the play page feed it the same
    views, so a replay and a live game animate alike.
  - `js/fx.js`: particles.
  - `js/sfx.js`: WebAudio.
  - `js/hud.js`: the shared HUD.

## Builds

The wasm targets are all `manual`, and `--config=web` (in `.bazelrc`) is what
compiles them:
- WebAssembly exceptions, because the rules throw;
- abseil's use of emscripten's deprecated version macros, allowed.

A candidate's warnings are its author's, so its code builds with
`-Wno-error`.

A rival's code is only *compiled* on this host, from the header it submitted,
with the arena's generated BUILD (no genrules). It *runs* in the browser's
sandbox: the same trust as `arena_cli spar`, with less reach.
