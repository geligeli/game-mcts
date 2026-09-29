#!/usr/bin/env python3
"""Serves web/'s 3D viewer and play page, and the games they show.

  python3 web/serve.py --data_dir ~/.arena/risk2-swiss-<time> \\
      --data_dir ~/.arena/risk2 [--port 8095] [--bind 0.0.0.0]

Read-only. Routes:
  /, /*.html, /static/...   the pages (web/static)
  /bots/<id>.{js,wasm}      a candidate compiled by web/build.sh (web/dist);
                            /bots/<id> is the .js, as its pthreads load it
  /api/bots                 with --leaderboard, the coordinator's top --top,
                            each compiled once it reaches it (stage.py and
                            build.sh, in the background); else
                            web/bots/manifest.json (stage.py), rated as the
                            data dirs' swiss.tsv last has them
  /api/games?player=&dir=&offset=&limit=
                            games, newest first, from each data dir's
                            games/index.jsonl, with "dir" added
  /api/game/<dir>/<id>      a GameRecord's bytes, as the coordinator stores it

Every response is cross-origin isolated (COOP + COEP), which is what lets a
page share memory with the threads of a candidate's WebAssembly, and every
link is relative, so a reverse proxy can mount it under a path
(takumi.games/sessions/).
"""

import argparse
import gzip
import http.server
import json
import mimetypes
import pathlib
import re
import subprocess
import sys
import threading
import time
import urllib.parse
import urllib.request

from stage import ratings

WEB = pathlib.Path(__file__).resolve().parent
SAFE_ID = re.compile(r"^[A-Za-z0-9_.-]+$")
TYPES = {".js": "text/javascript", ".mjs": "text/javascript",
         ".wasm": "application/wasm", ".glb": "model/gltf-binary",
         ".gltf": "model/gltf+json", ".bin": "application/octet-stream",
         ".ogg": "audio/ogg", ".wav": "audio/wav", ".svg": "image/svg+xml",
         ".json": "application/json", ".html": "text/html; charset=utf-8",
         ".css": "text/css", ".png": "image/png"}


class Games:
    """The merged game indexes, reread when a data dir's index changes."""

    def __init__(self, dirs):
        self.dirs = {d.name: d for d in dirs}
        self.cache = {}  # dir name -> (mtime, [entries])

    def entries(self, name):
        index = self.dirs[name] / "games" / "index.jsonl"
        if not index.exists():
            return []
        mtime = index.stat().st_mtime
        if self.cache.get(name, (None,))[0] != mtime:
            rows = []
            for line in index.read_text().splitlines():
                try:
                    row = json.loads(line)
                except ValueError:
                    continue
                row["dir"] = name
                rows.append(row)
            self.cache[name] = (mtime, rows)
        return self.cache[name][1]

    def query(self, player, dir_name, offset, limit):
        rows = [r for name in self.dirs if not dir_name or name == dir_name
                for r in self.entries(name)
                if not player or player in (r.get("player0"), r.get("player1"))]
        rows.sort(key=lambda r: -r.get("finished_unix_ms", 0))
        players = sorted({p for name in self.dirs for r in self.entries(name)
                          for p in (r.get("player0"), r.get("player1")) if p})
        return {"total": len(rows), "games": rows[offset:offset + limit],
                "dirs": list(self.dirs), "players": players}

    def record(self, dir_name, game_id):
        if dir_name not in self.dirs or not SAFE_ID.match(game_id):
            return None
        path = self.dirs[dir_name] / "games" / (game_id + ".pb")
        return path.read_bytes() if path.exists() else None


class TopBots:
    """The coordinator's top N by its leaderboard, kept compiled."""

    def __init__(self, leaderboard, top, stage_from, bots_dir, period=120):
        self.url = leaderboard.rstrip("/") + "/api/leaderboard"
        self.top, self.stage_from = top, stage_from
        self.bots_dir, self.period = bots_dir, period
        self.rows = []
        self.building = set()

    def poll(self):
        with urllib.request.urlopen(self.url, timeout=10) as response:
            rows = json.load(response)["rows"][:self.top]
        self.rows = rows
        missing = [r["candidate_id"] for r in rows
                   if not (self.bots_dir / (r["candidate_id"] + ".wasm")).exists()]
        if not missing:
            return
        self.building = set(missing)
        print(f"compiling {', '.join(missing)}", flush=True)
        # A version never changes: staged and built once, kept after.
        subprocess.run([sys.executable, str(WEB / "stage.py"),
                        str(self.stage_from), "--only", ",".join(missing),
                        "--keep"], check=True)
        subprocess.run(["bash", str(WEB / "build.sh"), *missing], check=True)
        self.building = set()

    def run(self):
        while True:
            try:
                self.poll()
            except Exception as error:  # the board may be down for a while
                print(f"top bots: {error}", flush=True)
                self.building = set()
            time.sleep(self.period)

    def entries(self):
        entries = []
        for r in self.rows:
            m = re.fullmatch(r"(.+)-v(\d+)", r["candidate_id"])
            entries.append({
                "id": r["candidate_id"],
                "participant": r.get("author") or (m[1] if m else r["candidate_id"]),
                "version": int(m[2]) if m else 0,
                "rank": r["rank"], "score": r["score"],
                "mu": r.get("mu"), "sigma": r.get("sigma"),
                "built": (self.bots_dir / (r["candidate_id"] + ".wasm")).exists(),
            })
        if (self.bots_dir / "reference.wasm").exists():
            entries.append({"id": "reference", "participant": "reference",
                            "version": 0, "built": True})
        return entries


class Handler(http.server.BaseHTTPRequestHandler):
    games = None
    bots_dir = None
    top_bots = None
    gzipped = {}  # path -> (mtime, bytes)

    def log_message(self, fmt, *args):
        pass

    def send(self, body, content_type, status=200, cache="no-cache",
             encoding=None):
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", cache)
        self.send_header("Cross-Origin-Opener-Policy", "same-origin")
        self.send_header("Cross-Origin-Embedder-Policy", "require-corp")
        self.send_header("Cross-Origin-Resource-Policy", "same-origin")
        if encoding:
            self.send_header("Content-Encoding", encoding)
        self.end_headers()
        if self.command != "HEAD":
            self.wfile.write(body)

    def send_file(self, path, cache):
        path = path.resolve()
        if not path.is_file():
            return self.send(b"not found\n", "text/plain", 404)
        content_type = TYPES.get(path.suffix) or mimetypes.guess_type(
            path.name)[0] or "application/octet-stream"
        # The big ones are worth compressing once; nginx's gzip_types do not
        # cover wasm or models.
        if (path.suffix in (".wasm", ".glb", ".bin", ".js", ".wav")
                and "gzip" in self.headers.get("Accept-Encoding", "")):
            mtime = path.stat().st_mtime
            cached = self.gzipped.get(path)
            if not cached or cached[0] != mtime:
                cached = (mtime, gzip.compress(path.read_bytes(), 6))
                self.gzipped[path] = cached
            return self.send(cached[1], content_type, cache=cache,
                             encoding="gzip")
        return self.send(path.read_bytes(), content_type, cache=cache)

    def do_HEAD(self):
        self.do_GET()

    def do_GET(self):
        url = urllib.parse.urlsplit(self.path)
        route = urllib.parse.unquote(url.path)
        query = {k: v[0] for k, v in urllib.parse.parse_qs(url.query).items()}
        if route in ("/", "/index.html"):
            return self.send_file(WEB / "static" / "index.html", "no-cache")
        if re.fullmatch(r"/[a-z_]+\.html", route):
            return self.send_file(WEB / "static" / route[1:], "no-cache")
        if route.startswith("/static/") and ".." not in route:
            long_lived = route.startswith(("/static/assets/",
                                           "/static/vendor/"))
            return self.send_file(WEB / route[1:], "max-age=86400"
                                  if long_lived else "no-cache")
        m = re.fullmatch(r"/bots/([A-Za-z0-9_.-]+\.(?:js|wasm))", route)
        if m:
            return self.send_file(self.bots_dir / m[1], "no-cache")
        # A module's pthread workers load it by its bare name.
        m = re.fullmatch(r"/bots/([A-Za-z0-9_-]+(?:\.[A-Za-z0-9_-]+)*)", route)
        if m and (self.bots_dir / (m[1] + ".js")).exists():
            return self.send_file(self.bots_dir / (m[1] + ".js"), "no-cache")
        if route == "/api/bots" and self.top_bots is not None:
            return self.send(json.dumps(self.top_bots.entries()).encode(),
                             TYPES[".json"])
        if route == "/api/bots":
            manifest = WEB / "bots" / "manifest.json"
            entries = json.loads(manifest.read_text()) if manifest.exists() else []
            built = [e for e in entries
                     if (self.bots_dir / (e["id"] + ".wasm")).exists()]
            # The re-rank may still be running: its latest ratings, not the
            # ones stage.py saw.
            live = {}
            for d in self.games.dirs.values():
                live.update(ratings(d))
            for e in built:
                e["mu"], e["sigma"] = live.get(e["id"], (e["mu"], e["sigma"]))
            built.sort(key=lambda e: -(e["mu"] if e["mu"] is not None else -1e9))
            return self.send(json.dumps(built).encode(), TYPES[".json"])
        if route == "/api/games":
            result = self.games.query(query.get("player", ""),
                                      query.get("dir", ""),
                                      int(query.get("offset", 0)),
                                      min(int(query.get("limit", 50)), 500))
            return self.send(json.dumps(result).encode(), TYPES[".json"])
        m = re.fullmatch(r"/api/game/([^/]+)/([^/]+)", route)
        if m:
            record = self.games.record(m[1], m[2])
            if record is None:
                return self.send(b"no such game\n", "text/plain", 404)
            return self.send(record, "application/octet-stream",
                             cache="max-age=86400")
        return self.send(b"not found\n", "text/plain", 404)


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--port", type=int, default=8095)
    parser.add_argument("--bind", default="0.0.0.0")
    parser.add_argument("--data_dir", type=pathlib.Path, action="append",
                        default=[], help="an arena data dir; repeatable")
    parser.add_argument("--bots_dir", type=pathlib.Path,
                        default=WEB / "dist" / "bots")
    parser.add_argument("--leaderboard", default="",
                        help="a coordinator's address, e.g. http://localhost:8090: "
                             "serve its top --top, compiled as they get there")
    parser.add_argument("--top", type=int, default=10)
    parser.add_argument("--stage_from", type=pathlib.Path,
                        default=pathlib.Path("~/.arena/risk2"),
                        help="the coordinator's data dir, for the candidates' code")
    args = parser.parse_args()
    Handler.games = Games([d.expanduser().resolve() for d in args.data_dir])
    Handler.bots_dir = args.bots_dir
    if args.leaderboard:
        Handler.top_bots = TopBots(args.leaderboard, args.top,
                                   args.stage_from.expanduser(), args.bots_dir)
        threading.Thread(target=Handler.top_bots.run, daemon=True).start()
    server = http.server.ThreadingHTTPServer((args.bind, args.port), Handler)
    print(f"serving on http://{args.bind}:{args.port}/", flush=True)
    server.serve_forever()


if __name__ == "__main__":
    main()
