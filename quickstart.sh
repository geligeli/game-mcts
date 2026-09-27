#!/usr/bin/env bash
# A risk2 tournament with coding agents playing it, in one tmux session: the
# coordinator, a sandbox worker, and one kit container per player, each running
# a coding agent on the standing mission in problem/mission.md.
#
#   ./quickstart.sh            images pushed to registry.takumi.city
#   ./quickstart.sh --local    images kept in the local docker daemon
#   PLAYERS="ann ben cy" ./quickstart.sh
#   GRPC_PORT=50061 HTTP_PORT=8091 ./quickstart.sh   beside another tournament
#   AGENT=opencode ./quickstart.sh     opencode on a free OpenCode Zen model
#   AGENT=opencode MODEL=opencode/<model> ./quickstart.sh
#
# Needs tmux and docker; for AGENT=claude (the default) CLAUDE_CODE_OAUTH_TOKEN
# (`claude setup-token`), and for the default mode a
# `docker login registry.takumi.city`.
# The agents' base image is game-arena's (its quickstart.sh builds it); here it
# is only pulled, by the digest in MODULE.bazel.
set -euo pipefail

LOCAL=0
[[ "${1:-}" == "--local" ]] && LOCAL=1
TAG=${TAG:-latest}
KIT=${KIT:-registry.takumi.city/game-mcts-kit}
PLAYERS=${PLAYERS:-"alice bob"}
SESSION=${SESSION:-game-mcts-risk2}
GRPC_PORT=${GRPC_PORT:-50051}
HTTP_PORT=${HTTP_PORT:-8090}
AGENT=${AGENT:-claude}
MODEL=${MODEL:-opencode/big-pickle}
CLIENTS=$HOME/.arena/risk2/clients.textproto

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"
cd "$DIR"

for tool in tmux docker bazel; do
  command -v $tool >/dev/null || { echo "quickstart: $tool is required" >&2; exit 1; }
done
case $AGENT in
  claude)
    [[ -n "${CLAUDE_CODE_OAUTH_TOKEN:-}" ]] ||
      echo "quickstart: CLAUDE_CODE_OAUTH_TOKEN is unset; the agents will ask you to log in" >&2
    RUN_AGENT='claude --dangerously-skip-permissions "$(cat /mission.md)"' ;;
  opencode)
    RUN_AGENT="opencode -m $MODEL --prompt \"\$(cat /mission.md)\"" ;;
  *) echo "quickstart: AGENT is claude or opencode, not $AGENT" >&2; exit 1 ;;
esac
for port in $GRPC_PORT $HTTP_PORT; do
  if ss -Hltn "sport = :$port" | grep -q .; then
    echo "quickstart: port $port is taken; set GRPC_PORT / HTTP_PORT" >&2
    exit 1
  fi
done
if tmux has-session -t "$SESSION" 2>/dev/null; then
  echo "quickstart: tmux session $SESSION exists; tmux kill-session -t $SESSION" >&2
  exit 1
fi

# What sandbox.image in problem.textproto names: the image a worker asks for.
SANDBOX=$(sed -n '/^sandbox {/,/^}/s/^ *image: "\(.*\)"/\1/p' problem.textproto)
if ((LOCAL)); then
  bazel run -c opt //:sandbox_image_issue -- --prime_bazelrc=prime.bazelrc
  bazel run -c opt //:kit_image_issue -- --image="$KIT:$TAG" \
    --prime_bazelrc=prime.bazelrc
  PULL=never
else
  bazel run -c opt //:sandbox_image_issue -- --prime_bazelrc=prime.bazelrc --push
  # Pulled back, so the worker here does not run a stale tag.
  docker pull "$SANDBOX"
  bazel run -c opt //:kit_image_issue -- --image="$KIT:$TAG" \
    --prime_bazelrc=prime.bazelrc --push
  PULL=always
fi

mint() {
  bazel run @game_arena//game_arena/tools:arena_admin -- mint --client_id="$1" \
    --clients="$CLIENTS" --overwrite
}

tmux new-session -d -s "$SESSION" -c "$DIR" \
  "bazel run -c opt //:tournament -- --grpc_port=$GRPC_PORT --http_port=$HTTP_PORT; exec bash"
tmux split-window -h -t "$SESSION" -c "$DIR" \
  "bazel run -c opt @game_arena//game_arena/sandbox/worker:sandbox_worker -- --server=localhost:$GRPC_PORT; exec bash"

# Each pane gets the token from here, not from the tmux server's environment;
# the mission is mounted. The entrypoint makes bots/<name>/ before claude
# starts: restored from the last submission, or the starter.
split=-vf
for name in $PLAYERS; do
  token=$(mint "$name")
  tmux split-window $split -t "$SESSION" -c "$DIR" \
    -e "CLAUDE_CODE_OAUTH_TOKEN=${CLAUDE_CODE_OAUTH_TOKEN:-}" \
    "docker run -it --rm --pull=$PULL --network host \
      -e ARENA_SERVER=localhost:$GRPC_PORT -e ARENA_NAME=$name -e ARENA_TOKEN=$token \
      -e ARENA_RESTORE=1 -e CLAUDE_CODE_OAUTH_TOKEN \
      -v $DIR/problem/mission.md:/mission.md:ro \
      $KIT:$TAG bash -c '$RUN_AGENT; exec bash'"
  split=-h
done
tmux select-layout -t "$SESSION" tiled >/dev/null

echo "risk2: dashboard http://localhost:$HTTP_PORT, tmux session $SESSION"
if [[ -t 1 ]]; then
  tmux attach-session -t "$SESSION"
fi
