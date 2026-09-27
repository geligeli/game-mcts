#!/usr/bin/env bash
# A risk2 tournament with coding agents playing it, in one tmux session: the
# coordinator, sandbox workers, and one kit container per player, each running
# a coding agent on the standing mission in problem/mission.md.
#
#   ./quickstart.sh            images pushed to registry.takumi.city
#   ./quickstart.sh --local    images kept in the local docker daemon, and
#                              copied to the workers' hosts
#   PLAYERS="ann:claude ben:opencode" ./quickstart.sh
#   WORKERS="local" ./quickstart.sh    one worker, on this host's docker
#   GRPC_PORT=50061 HTTP_PORT=8091 ./quickstart.sh   beside another tournament
#   MODEL=opencode/<model> ./quickstart.sh     the opencode players' model
#
# PLAYERS: name[:agent] each, the agent (claude, agy or opencode) defaulting
# to the name. claude needs CLAUDE_CODE_OAUTH_TOKEN (`claude setup-token`),
# agy GEMINI_API_KEY; unset, that agent asks you to log in in its pane.
# opencode runs a free OpenCode Zen model.
#
# WORKERS: one sandbox worker per entry, an ssh host or `local`. A worker is a
# process here; its builds and matches run on that host's docker
# (DOCKER_HOST=ssh://<host>), which needs nothing but docker there.
#
# Needs tmux and docker, and for the default mode a
# `docker login registry.takumi.city` here and on the workers' hosts.
# The agents' base image is game-arena's (its quickstart.sh builds it); here it
# is only pulled, by the digest in MODULE.bazel.
set -euo pipefail

LOCAL=0
[[ "${1:-}" == "--local" ]] && LOCAL=1
TAG=${TAG:-latest}
KIT=${KIT:-registry.takumi.city/game-mcts-kit}
PLAYERS=${PLAYERS:-"claude agy opencode"}
WORKERS=${WORKERS:-"geli-3950 geli-3950"}
AGENT_CPUS=${AGENT_CPUS:-4}
SESSION=${SESSION:-game-mcts-risk2}
GRPC_PORT=${GRPC_PORT:-50051}
HTTP_PORT=${HTTP_PORT:-8090}
MODEL=${MODEL:-opencode/big-pickle}
CLIENTS=$HOME/.arena/risk2/clients.textproto

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"
cd "$DIR"

for tool in tmux docker bazel; do
  command -v $tool >/dev/null || { echo "quickstart: $tool is required" >&2; exit 1; }
done
names=() runs=() secrets=()
for player in $PLAYERS; do
  agent=${player#*:}
  case $agent in
    claude)
      run='claude --dangerously-skip-permissions "$(cat /mission.md)"'
      secret=CLAUDE_CODE_OAUTH_TOKEN ;;
    agy)
      run='agy --dangerously-skip-permissions -i "$(cat /mission.md)"'
      secret=GEMINI_API_KEY ;;
    opencode)
      # --auto: approve whatever is not denied, e.g. reading /tmp/spar.* logs.
      run="opencode --auto -m $MODEL --prompt \"\$(cat /mission.md)\""
      secret= ;;
    *) echo "quickstart: $player: the agent is claude, agy or opencode" >&2; exit 1 ;;
  esac
  if [[ -n $secret && -z ${!secret:-} ]]; then
    echo "quickstart: $secret is unset; ${player%%:*} will ask you to log in" >&2
  fi
  names+=("${player%%:*}") runs+=("$run") secrets+=("$secret")
done
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
  bazel run -c opt //:kit_image_issue -- --image="$KIT:$TAG" \
    --prime_bazelrc=prime.bazelrc --push
  PULL=always
fi
# Onto each worker's docker, so none runs a stale tag or has to fetch one.
for host in $(printf '%s\n' $WORKERS | sort -u); do
  if [[ $host == local ]]; then
    ((LOCAL)) || docker pull "$SANDBOX"
    continue
  fi
  ssh "$host" true  # an unknown host key is asked about here, not in a pane
  if ((LOCAL)); then
    docker save "$SANDBOX" | docker -H "ssh://$host" load
  else
    docker -H "ssh://$host" pull "$SANDBOX"
  fi
done

mint() {
  bazel run @game_arena//game_arena/tools:arena_admin -- mint --client_id="$1" \
    --clients="$CLIENTS" --overwrite
}

pane() {  # <tmux split-window flags...> <command>
  tmux split-window -t "$SESSION" -c "$DIR" "$@"
  tmux select-layout -t "$SESSION" tiled >/dev/null
}

tmux new-session -d -s "$SESSION" -c "$DIR" \
  "bazel run -c opt //:tournament -- --grpc_port=$GRPC_PORT --http_port=$HTTP_PORT; exec bash"

# Workers on one docker need their own volumes; each gets its own work dir.
worker=0
for host in $WORKERS; do
  worker=$((worker + 1))
  remote=()
  [[ $host == local ]] || remote=(-e "DOCKER_HOST=ssh://$host")
  pane "${remote[@]}" -e "ARENA_VOLUME_PREFIX=$SESSION-w$worker" \
    -e "ARENA_WORK_DIR=/tmp/arena_sandbox/$SESSION-w$worker" \
    "bazel run -c opt @game_arena//game_arena/sandbox/worker:sandbox_worker -- --server=localhost:$GRPC_PORT; exec bash"
done

# Each pane gets its agent's secret from here, not from the tmux server's
# environment; the mission is mounted. The entrypoint makes bots/<name>/
# before the agent starts: restored from the last submission, or the starter.
for i in "${!names[@]}"; do
  name=${names[i]} secret=${secrets[i]}
  token=$(mint "$name")
  pane ${secret:+-e "$secret=${!secret:-}"} \
    "docker run -it --rm --pull=$PULL --network host --cpus=$AGENT_CPUS \
      -e ARENA_SERVER=localhost:$GRPC_PORT -e ARENA_NAME=$name -e ARENA_TOKEN=$token \
      -e ARENA_RESTORE=1 ${secret:+-e $secret} \
      -v $DIR/problem/mission.md:/mission.md:ro \
      $KIT:$TAG bash -c '${runs[i]}; exec bash'"
done

# The coordinator listens on every interface; this is the address others use.
ADDRESS=$(ip -4 route get 1.1.1.1 | sed -n 's/.* src \([0-9.]*\).*/\1/p')
echo "risk2: dashboard http://${ADDRESS:-localhost}:$HTTP_PORT, tmux session $SESSION"
if [[ -t 1 ]]; then
  tmux attach-session -t "$SESSION"
fi
