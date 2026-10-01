#!/usr/bin/env python3
"""Serves the full session transcripts of the coding agents in the kit containers.

Read-only: it only runs `docker ps`, `docker inspect` and `docker exec` of
`cat` / a read-only sqlite query. It has no auth, so every secret-looking env
value of the kit containers is redacted from what it serves. Links are
relative, so a reverse proxy can mount it under a path (takumi.games/sessions/).

  python3 tools/agent_logs.py [--port 8095] [--bind 0.0.0.0]
"""

import argparse
import datetime
import html
import http.server
import json
import re
import shlex
import subprocess
import urllib.parse

HOME = "/home/ubuntu"
SECRET_ENV = re.compile(r"TOKEN|KEY|SECRET|PASSWORD", re.I)
RESULT_HTML_LIMIT = 8192

# Run inside the opencode container: only the message and part tables (the db
# also holds account credentials), opened read-only so the agent's WAL is safe.
OPENCODE_QUERY = f"""
import json, sqlite3
db = sqlite3.connect("file:{HOME}/.local/share/opencode/opencode.db?mode=ro", uri=True)
for role, t, data in db.execute(
    "select json_extract(m.data, '$.role'), p.time_created, p.data from part p "
    "join message m on m.id = p.message_id order by m.time_created, p.id"):
  print(json.dumps([role, t, json.loads(data)]))
"""


def run(*cmd):
  return subprocess.run(cmd, capture_output=True, text=True, check=True).stdout


def containers(image):
  ids = run("docker", "ps", "-q", "--filter", f"ancestor={image}").split()
  if not ids:
    return []
  agents = []
  for c in json.loads(run("docker", "inspect", *ids)):
    env = dict(e.split("=", 1) for e in c["Config"]["Env"] if "=" in e)
    cmd = " ".join(c["Config"]["Cmd"] or [])
    kind = next((k for k, word in (("claude", "claude "), ("kimi", "kimi "),
                                   ("opencode", "opencode "), ("agy", "agy "))
                 if word in cmd), "unknown")
    agents.append({
        "name": env.get("ARENA_NAME", c["Name"].lstrip("/")),
        "container": c["Id"][:12],
        "kind": kind,
        "secrets": [v for k, v in env.items()
                    if SECRET_ENV.search(k) and len(v) >= 8],
    })
  for a in agents:
    a["peers"] = [b["name"] for b in agents if b["kind"] == a["kind"]]
  return sorted(agents, key=lambda a: a["name"])


def exec_in(container, script):
  return run("docker", "exec", container, "sh", "-c", script)


def iso(t):
  """Local time; Kimi and opencode give epoch ms, Claude Code UTC ISO strings."""
  if isinstance(t, (int, float)):
    when = datetime.datetime.fromtimestamp(t / 1000)
  elif t:
    when = datetime.datetime.fromisoformat(t.replace("Z", "+00:00")).astimezone()
  else:
    return ""
  return when.strftime("%Y-%m-%d %H:%M:%S")


def text_of(content):
  if isinstance(content, str):
    return content
  return "\n".join(p.get("text", "") for p in content or [] if isinstance(p, dict))


def args_summary(args):
  if isinstance(args, dict) and isinstance(args.get("command"), str):
    return args["command"]
  return json.dumps(args, ensure_ascii=False)


def ev(time, kind, title, body):
  return {"time": iso(time), "kind": kind, "title": title, "body": body}


def records(out):
  # A line cut off by a full disk, or still being written, is skipped: one bad
  # line must not take down every page.
  for line in out.splitlines():
    try:
      yield json.loads(line)
    except ValueError:
      pass


def claude_events(agent):
  out = exec_in(agent["container"],
                f"ls -tr {HOME}/.claude/projects/*/*.jsonl | xargs -r cat")
  events = []
  for d in records(out):
    if d.get("type") not in ("user", "assistant") or d.get("isMeta"):
      continue
    t = d.get("timestamp")
    content = d["message"]["content"]
    if isinstance(content, str):
      events.append(ev(t, d["type"] if d["type"] == "user" else "text", "", content))
      continue
    for p in content:
      if p["type"] == "text":
        events.append(ev(t, "user" if d["type"] == "user" else "text", "", p["text"]))
      elif p["type"] == "thinking":
        events.append(ev(t, "thinking", "", p["thinking"]))
      elif p["type"] == "tool_use":
        events.append(ev(t, "tool_call", p["name"], args_summary(p["input"])))
      elif p["type"] == "tool_result":
        events.append(ev(t, "tool_result", "error" if p.get("is_error") else "",
                         text_of(p.get("content"))))
  return events


def kimi_events(agent):
  # Every kit shares the host's ~/.kimi-code: an agent's sessions are the ones
  # that name it more than any other Kimi agent, however it was prompted.
  names = " ".join(shlex.quote(n) for n in agent["peers"])
  out = exec_in(agent["container"],
                f"for f in $(ls -tr {HOME}/.kimi-code/sessions/*/session_*"
                "/agents/main/wire.jsonl); do "
                f"best=$(for n in {names}; do "
                'echo "$(grep -ow -- "$n" "$f" | wc -l) $n"; done | sort -rn | head -1); '
                f'[ "${{best#* }}" = {shlex.quote(agent["name"])} ] && '
                '[ "${best%% *}" -gt 0 ] && cat "$f"; done; true')
  events = []
  for d in records(out):
    t = d.get("time")
    if d["type"] == "context.append_message":
      m = d["message"]
      events.append(ev(t, "user" if m["role"] == "user" else "text", "",
                       text_of(m.get("content"))))
    elif d["type"] == "context.append_loop_event":
      e = d["event"]
      if e["type"] == "content.part":
        p = e["part"]
        if p.get("type") == "think":
          events.append(ev(t, "thinking", "", p.get("think", "")))
        else:
          events.append(ev(t, "text", "", p.get("text", "")))
      elif e["type"] == "tool.call":
        events.append(ev(t, "tool_call", e["name"], args_summary(e.get("args"))))
      elif e["type"] == "tool.result":
        r = e.get("result") or {}
        out = r.get("output")
        events.append(ev(t, "tool_result", "error" if r.get("isError") else "",
                         out if isinstance(out, str) else json.dumps(r, ensure_ascii=False)))
  return events


def opencode_events(agent):
  out = run("docker", "exec", agent["container"], "python3", "-c",
            OPENCODE_QUERY)
  events = []
  for role, t, p in records(out):
    if p["type"] == "text":
      events.append(ev(t, "user" if role == "user" else "text", "", p["text"]))
    elif p["type"] == "reasoning":
      events.append(ev(t, "thinking", "", p["text"]))
    elif p["type"] == "tool":
      s = p.get("state") or {}
      events.append(ev(t, "tool_call", p["tool"], args_summary(s.get("input"))))
      if "output" in s or "error" in s:
        events.append(ev(t, "tool_result", s.get("status", ""),
                         s.get("output") or s.get("error") or ""))
  return events


def agy_events(agent):
  # Antigravity keeps a transcript per conversation; a tool call's arguments
  # are JSON strings inside it.
  out = exec_in(agent["container"],
                f"ls -tr {HOME}/.gemini/antigravity-cli/brain/*/.system_generated"
                "/logs/transcript.jsonl | xargs -r cat")
  def decoded(value):
    try:
      return json.loads(value)
    except (TypeError, ValueError):
      return value
  events = []
  for d in records(out):
    t = d.get("created_at")
    if d["type"] == "USER_INPUT":
      events.append(ev(t, "user", "", d.get("content", "")))
    elif d["type"] == "PLANNER_RESPONSE":
      if d.get("thinking"):
        events.append(ev(t, "thinking", "", d["thinking"]))
      if d.get("content"):
        events.append(ev(t, "text", "", d["content"]))
      for call in d.get("tool_calls", []):
        args = {k: decoded(v) for k, v in (call.get("args") or {}).items()}
        events.append(ev(t, "tool_call", call["name"], args_summary(args)))
    else:  # a tool's result, or the system's error
      events.append(ev(t, "tool_result", "error" if d.get("error") else "",
                       d.get("error") or d.get("content", "")))
  return events


READERS = {"claude": claude_events, "kimi": kimi_events,
           "opencode": opencode_events, "agy": agy_events}


def load(agent):
  events = READERS.get(agent["kind"], lambda _: [])(agent)
  # Streaming and redacted reasoning leave empty parts behind.
  return [e for e in events if e["body"].strip() or e["kind"] == "tool_result"]


def redactor(agents):
  secrets = sorted({s for a in agents for s in a["secrets"]}, key=len, reverse=True)
  pattern = re.compile("|".join(map(re.escape, secrets))) if secrets else None
  return (lambda s: pattern.sub("<redacted>", s)) if pattern else (lambda s: s)


# Colors are variables so the dark theme swaps values, not rules: dark overrides
# of the rules themselves lose to whichever light rule comes later in the sheet.
STYLE = """
:root{color-scheme:light dark;--bg:#fff;--fg:#1d1d1f;--muted:#666;--link:#0b57d0;
  --rule:#d0d0d7;--user-bg:#eef3fb;--user:#0b57d0;--text:#1a7f37;--think:#bbb;
  --call-bg:#f1f8f1;--call:#9a6700;--result:#e3c98a;--pre-bg:#f6f6f8}
@media (prefers-color-scheme:dark){:root{--bg:#16161a;--fg:#e6e6e6;--muted:#9a9aa3;
  --link:#8ab4f8;--rule:#34343c;--user-bg:#1d2433;--user:#8ab4f8;--text:#3fb950;
  --think:#55555e;--call-bg:#1c261c;--call:#d29922;--result:#6b5520;--pre-bg:#0f0f12}}
body{font:14px/1.45 system-ui,sans-serif;margin:0 auto;max-width:1100px;padding:12px 16px;
     background:var(--bg);color:var(--fg)}
a{color:var(--link)}table{border-collapse:collapse;width:100%}
td,th{padding:6px 8px;border-bottom:1px solid var(--rule);text-align:left;vertical-align:top}
.ev{border-left:3px solid var(--rule);margin:6px 0;padding:2px 10px}
.meta{font-size:11px;color:var(--muted)}
.user{background:var(--user-bg);border-color:var(--user)}
.text{border-color:var(--text)}
.thinking{color:var(--muted);font-style:italic;border-color:var(--think)}
.tool_call{background:var(--call-bg);font-family:ui-monospace,monospace;font-size:12.5px;
           border-color:var(--call);white-space:pre-wrap;word-break:break-word}
.tool_result{border-color:var(--result)}
pre{white-space:pre-wrap;word-break:break-word;margin:4px 0;font-size:12.5px;
    background:var(--pre-bg);color:var(--fg);padding:6px 8px;max-height:40em;overflow:auto}
.body{white-space:pre-wrap;word-break:break-word}
"""


def page(title, body, refresh=None):
  head = f'<meta http-equiv="refresh" content="{int(refresh)}">' if refresh else ""
  return (f'<!doctype html><html><head><meta charset="utf-8">'
          f'<meta name="viewport" content="width=device-width,initial-scale=1">{head}'
          f"<title>{html.escape(title)}</title><style>{STYLE}</style></head>"
          f"<body>{body}</body></html>")


def render_event(e):
  esc = html.escape
  meta = f'<div class="meta">{esc(e["time"])} {esc(e["kind"])} {esc(e["title"])}</div>'
  if e["kind"] == "tool_call":
    return f'<div class="ev tool_call">{meta}{esc(e["title"])}: {esc(e["body"])}</div>'
  if e["kind"] == "tool_result":
    body = e["body"]
    cut = len(body) - RESULT_HTML_LIMIT
    if cut > 0:
      body = body[:RESULT_HTML_LIMIT] + f"\n... ({cut} more chars in the .jsonl view)"
    first = body.strip().splitlines()[0][:120] if body.strip() else "(empty)"
    return (f'<details class="ev tool_result"><summary>{meta}{esc(first)}</summary>'
            f"<pre>{esc(body)}</pre></details>")
  return f'<div class="ev {e["kind"]}">{meta}<div class="body">{esc(e["body"])}</div></div>'


class Handler(http.server.BaseHTTPRequestHandler):
  image = None

  def send(self, code, body, ctype="text/html; charset=utf-8"):
    data = body.encode()
    self.send_response(code)
    self.send_header("Content-Type", ctype)
    self.send_header("Content-Length", str(len(data)))
    self.end_headers()
    self.wfile.write(data)

  def do_GET(self):
    url = urllib.parse.urlparse(self.path)
    query = urllib.parse.parse_qs(url.query)
    try:
      agents = containers(self.image)
      redact = redactor(agents)
      if url.path == "/":
        self.send(200, redact(self.index(agents)))
        return
      m = re.fullmatch(r"/agent/([\w.-]+?)(\.jsonl)?", url.path)
      agent = next((a for a in agents if m and a["name"] == m.group(1)), None)
      if agent is None:
        self.send(404, page("not found", "<p>No such agent. <a href='../'>All agents</a></p>"))
        return
      events = load(agent)
      if m.group(2):
        self.send(200, redact("".join(json.dumps(e, ensure_ascii=False) + "\n"
                                      for e in events)),
                  "application/x-ndjson; charset=utf-8")
        return
      n = query.get("n", ["300"])[0]
      shown = events if n == "all" else events[-int(n):]
      refresh = query.get("refresh", [None])[0]
      name = html.escape(agent["name"])
      body = (f'<p><a href="../">All agents</a> · <b>{name}</b> ({agent["kind"]}) · '
              f"showing {len(shown)} of {len(events)} events · "
              f'<a href="?n=all">all</a> · <a href="?n={html.escape(n)}&refresh=15">auto-refresh</a> · '
              f'<a href="{name}.jsonl">jsonl</a> · <a href="#end">bottom</a></p>'
              + "".join(map(render_event, shown)) + '<p id="end"></p>'
              "<script>location.hash||scrollTo(0,document.body.scrollHeight)</script>")
      self.send(200, redact(page(f"{agent['name']} transcript", body, refresh)))
    except (subprocess.CalledProcessError, ValueError) as e:
      self.send(500, page("error", f"<pre>{html.escape(str(e))}\n"
                          f"{html.escape(getattr(e, 'stderr', '') or '')}</pre>"))

  def index(self, agents):
    rows = []
    for a in agents:
      events = load(a)
      last_text = next((e["body"] for e in reversed(events) if e["kind"] == "text"), "")
      last_text = last_text.strip().splitlines()[-1][:200] if last_text.strip() else ""
      name = html.escape(a["name"])
      rows.append(f'<tr><td><a href="agent/{name}">{name}</a></td><td>{a["kind"]}</td>'
                  f"<td>{len(events)}</td><td>{events[-1]['time'] if events else ''}</td>"
                  f"<td>{html.escape(last_text)}</td></tr>")
    return page("Agent transcripts",
                "<h2>Agent transcripts</h2><table><tr><th>agent</th><th>kind</th>"
                "<th>events</th><th>last activity</th><th>last message</th></tr>"
                + "".join(rows) + "</table>", refresh=30)

  def log_message(self, fmt, *args):
    pass


def main():
  parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
  parser.add_argument("--port", type=int, default=8095)
  parser.add_argument("--bind", default="0.0.0.0")
  parser.add_argument("--image", default="registry.takumi.city/game-mcts-kit:latest")
  args = parser.parse_args()
  Handler.image = args.image
  server = http.server.ThreadingHTTPServer((args.bind, args.port), Handler)
  print(f"agent_logs: http://{args.bind}:{args.port}/", flush=True)
  server.serve_forever()


if __name__ == "__main__":
  main()
