#!/usr/bin/env bash
# Smoke test of the dedicated server:
#   1. a game with one human slot and one computer empire on a free local port,
#      and a scripted client (the server's "bot" mode) that plays two turns;
#   2. a turn-based game with two bots and a computer empire, each bot playing
#      one command in each of its turns (commands carried out at once);
#   3. a turn-based play-by-e-mail game: each player's (empty) turn file is
#      processed in turn, and the game goes on to the next player.
# Needs a data set (the installed classic game is found automatically, or pass
# --data=DIR).
#
#   tools/server_smoke.sh [BUILD_DIR] [--data=DIR]
set -euo pipefail

BUILD=build/debug
if [[ $# -gt 0 && $1 != --* ]]; then
    BUILD=$1
    shift
fi
SERVER="$BUILD/opense4-server"
WORK=$(mktemp -d)
PID=
cleanup() {
    [[ -n $PID ]] && kill "$PID" 2>/dev/null || true
    rm -rf "$WORK"
}
trap cleanup EXIT

# The port a server started with --port=0 listens on (from its log).
wait_port() {
    local log=$1 port=
    for _ in $(seq 100); do
        port=$(sed -n 's/.*waiting for players on TCP port \([0-9]*\).*/\1/p' "$log")
        [[ -n $port ]] && break
        kill -0 "$PID" 2>/dev/null || break
        sleep 0.1
    done
    if [[ -z $port ]]; then
        cat "$log" >&2
        echo "server smoke test FAILED: the server did not start" >&2
        exit 1
    fi
    echo "$port"
}

# 1. Simultaneous.
"$SERVER" --port=0 --players=1 --ai=1 --no-upnp --no-lan-discovery --seed=7 --systems=20 --name=Smoke \
    --save-dir="$WORK" --max-turns=2 "$@" >"$WORK/server.log" 2>&1 &
PID=$!
PORT=$(wait_port "$WORK/server.log")
"$SERVER" bot --connect="127.0.0.1:$PORT" --name=smoke --password=secret --turns=2 "$@"
wait "$PID"
PID=
cat "$WORK/server.log"
grep -q "Reached turn 2" "$WORK/server.log"
"$SERVER" pbem info --game="$WORK/Smoke.gam"

# 2. Turn-based, over the network.
"$SERVER" --port=0 --players=2 --ai=1 --turn-based --no-upnp --no-lan-discovery --seed=7 --systems=20 --name=Relay \
    --save-dir="$WORK" --max-turns=2 "$@" >"$WORK/relay.log" 2>&1 &
PID=$!
PORT=$(wait_port "$WORK/relay.log")
"$SERVER" bot --connect="127.0.0.1:$PORT" --name=alice --password=a --turns=2 "$@" >"$WORK/alice.log" 2>&1 &
ALICE=$!
"$SERVER" bot --connect="127.0.0.1:$PORT" --name=bob --password=b --turns=2 "$@" >"$WORK/bob.log" 2>&1 || { cat "$WORK/bob.log" "$WORK/relay.log"; exit 1; }
wait "$ALICE" || { cat "$WORK/alice.log" "$WORK/relay.log"; exit 1; }
wait "$PID"
PID=
cat "$WORK/relay.log"
grep -q "Reached turn 2" "$WORK/relay.log"
grep -q "was carried out" "$WORK/alice.log"
"$SERVER" pbem info --game="$WORK/Relay.gam"

# 3. Turn-based, by e-mail.
cat >"$WORK/mail.toml" <<'EOF'
name = "Mail Relay"
seed = 3
[options]
systems = 12
simultaneous = false
[[empire]]
kind = "human"
player = "alice"
password = "a"
[[empire]]
kind = "human"
player = "bob"
password = "b"
[[empire]]
kind = "computer"
EOF
mkdir "$WORK/inbox"
"$SERVER" pbem new --setup="$WORK/mail.toml" --out="$WORK/mail.gam" "$@"
"$SERVER" pbem orders --game="$WORK/mail.gam" --empire=1 --password=a --out="$WORK/inbox"
"$SERVER" pbem process --game="$WORK/mail.gam" --orders="$WORK/inbox" "$@" | tee "$WORK/process1.log"
grep -q "Next: empire 2" "$WORK/process1.log"
"$SERVER" pbem orders --game="$WORK/mail.gam" --empire=2 --password=b --out="$WORK/inbox"
"$SERVER" pbem process --game="$WORK/mail.gam" --orders="$WORK/inbox" "$@" | tee "$WORK/process2.log"
grep -q "the game is now at turn 1" "$WORK/process2.log"
grep -q "Next: empire 1" "$WORK/process2.log"
"$SERVER" pbem info --game="$WORK/mail.gam"
echo "server smoke test passed"
