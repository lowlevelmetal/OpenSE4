#!/usr/bin/env bash
# Smoke test of the dedicated server: a game with one human slot and one
# computer empire on a free local port, and a scripted client (the server's
# "bot" mode) that plays two turns. Needs a data set (the installed classic
# game is found automatically, or pass --data=DIR).
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

"$SERVER" --port=0 --players=1 --ai=1 --no-upnp --seed=7 --systems=20 --name=Smoke \
    --save-dir="$WORK" --max-turns=2 "$@" >"$WORK/server.log" 2>&1 &
PID=$!

PORT=
for _ in $(seq 100); do
    PORT=$(sed -n 's/.*waiting for players on TCP port \([0-9]*\).*/\1/p' "$WORK/server.log")
    [[ -n $PORT ]] && break
    kill -0 "$PID" 2>/dev/null || break
    sleep 0.1
done
if [[ -z $PORT ]]; then
    cat "$WORK/server.log"
    echo "server smoke test FAILED: the server did not start" >&2
    exit 1
fi

"$SERVER" bot --connect="127.0.0.1:$PORT" --name=smoke --password=secret --turns=2 "$@"
wait "$PID"
PID=
cat "$WORK/server.log"
grep -q "Reached turn 2" "$WORK/server.log"
"$SERVER" pbem info --game="$WORK/Smoke.gam"
echo "server smoke test passed"
