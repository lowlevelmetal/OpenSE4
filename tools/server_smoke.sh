#!/usr/bin/env bash
# Smoke test of the dedicated server:
#   1. a game with one human slot and one computer empire on a free local port,
#      and a scripted client (the server's "bot" mode) that plays two turns;
#   2. a turn-based game with two bots and a computer empire, each bot playing
#      one command in each of its turns (commands carried out at once);
#   3. a turn-based play-by-e-mail game: each player's (empty) orders, made
#      from their own turn file, are processed in turn, and the game goes on to
#      the next player;
#   4. when the game client was built (and SMOKE_CLIENT is not 0), it plays the
#      next turn of that game offscreen from the player's turn file (--pbem ...
#      --pbem-end-turn), and the server processes the .plr it wrote.
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
    --save-dir="$WORK" --host-key="$WORK/host_key.txt" --max-turns=2 "$@" >"$WORK/server.log" 2>&1 &
PID=$!
PORT=$(wait_port "$WORK/server.log")
# The bot refuses any host but the one with this key.
HOSTKEY=$(sed -n 's/.*public key \([0-9a-f]\{64\}\).*/\1/p' "$WORK/server.log")
"$SERVER" bot --connect="127.0.0.1:$PORT" --name=smoke --password=secret --turns=2 --host-key="$HOSTKEY" "$@"
wait "$PID"
PID=
cat "$WORK/server.log"
grep -q "Reached turn 2" "$WORK/server.log"
"$SERVER" pbem info --game="$WORK/Smoke.gam"

# 2. Turn-based, over the network.
"$SERVER" --port=0 --players=2 --ai=1 --turn-based --no-upnp --no-lan-discovery --seed=7 --systems=20 --name=Relay \
    --save-dir="$WORK" --host-key="$WORK/host_key.txt" --max-turns=2 "$@" >"$WORK/relay.log" 2>&1 &
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
"$SERVER" pbem new --setup="$WORK/mail.toml" --out="$WORK/mail.gam" "$@" | tee "$WORK/new.log"
grep -q "Send .*Mail_Relay_01.turn to empire 1" "$WORK/new.log"
"$SERVER" pbem info --game="$WORK/Mail_Relay_01.turn"
"$SERVER" pbem orders --turn="$WORK/Mail_Relay_01.turn" --password=a --out="$WORK/inbox"
"$SERVER" pbem process --game="$WORK/mail.gam" --orders="$WORK/inbox" "$@" | tee "$WORK/process1.log"
grep -q "Next: empire 2" "$WORK/process1.log"
grep -q "Send .*Mail_Relay_02.turn to empire 2" "$WORK/process1.log"
"$SERVER" pbem orders --turn="$WORK/Mail_Relay_02.turn" --password=b --out="$WORK/inbox"
"$SERVER" pbem process --game="$WORK/mail.gam" --orders="$WORK/inbox" "$@" | tee "$WORK/process2.log"
grep -q "the game is now at turn 1" "$WORK/process2.log"
grep -q "Next: empire 1" "$WORK/process2.log"
"$SERVER" pbem info --game="$WORK/mail.gam"

# 4. The game client plays a turn of the e-mail game.
CLIENT="$BUILD/opense4"
if [[ -x $CLIENT && ${SMOKE_CLIENT:-1} != 0 ]]; then
    CLIENT_ARGS=()
    for a in "$@"; do
        [[ $a == --data=* ]] && CLIENT_ARGS+=("--classic-dir=${a#--data=}")
    done
    SDL_VIDEO_DRIVER=offscreen "$CLIENT" --no-audio --pbem="$WORK/Mail_Relay_01.turn" --pbem-password=a --pbem-orders="$WORK/inbox" \
        --pbem-end-turn "${CLIENT_ARGS[@]}" | tee "$WORK/client.log"
    grep -q "Orders saved to .*Mail_Relay_01.plr" "$WORK/client.log"
    "$SERVER" pbem process --game="$WORK/mail.gam" --orders="$WORK/inbox" "$@" | tee "$WORK/process3.log"
    grep -q "orders: " "$WORK/process3.log"
    grep -q "Next: empire 2" "$WORK/process3.log"
fi
echo "server smoke test passed"
