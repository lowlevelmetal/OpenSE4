#!/usr/bin/env bash
# CI helper (.github/workflows/ci.yml): runs a doctest binary in several
# processes at once and fails when any of them fails. The test cases are dealt
# out in turn, so that the few long ones (whole games, kept next to each other
# in the sources) land in different processes. Every log is shown, a failing
# one unfolded.
#
#   run_tests_parallel.sh <test binary> <processes> [doctest options...]
#
# The tests may run side by side: their scratch directories and ports are
# unique to each process (tests/temp_dir.hpp, port 0).

set -uo pipefail

bin=$1
jobs=$2
shift 2

# The names, one per line between doctest's two rules of "=". (A read loop, not
# mapfile: macOS's own bash is version 3.2.)
names=()
while IFS= read -r name; do
    names+=("$name")
done < <("$bin" --list-test-cases "$@" | awk '/^=+$/ { rule++; next } rule == 1')
total=${#names[@]}
if [ "$total" -lt "$jobs" ]; then
    echo "Too few test cases to share out: $total" >&2
    exit 1
fi

# A --test-case filter that names exactly these tests: "," and "\" escaped.
filters=()
for ((k = 0; k < jobs; ++k)); do filters+=(""); done
for ((i = 0; i < total; ++i)); do
    name=${names[$i]//\\/\\\\}
    name=${name//,/\\,}
    k=$((i % jobs))
    filters[$k]+="${filters[$k]:+,}$name"
done

logs=$(mktemp -d)
trap 'rm -rf "$logs"' EXIT
pids=()
for ((k = 0; k < jobs; ++k)); do
    "$bin" "$@" --test-case="${filters[$k]}" > "$logs/$k.log" 2>&1 &
    pids+=($!)
done

status=0
for ((k = 0; k < jobs; ++k)); do
    title="Process $((k + 1)) of $jobs: test cases $((k + 1)), $((k + 1 + jobs)), ... of $total"
    if wait "${pids[$k]}"; then
        echo "::group::$title: passed"
        cat "$logs/$k.log"
        echo "::endgroup::"
    else
        code=$?
        status=1
        echo "$title: FAILED (exit status $code)"
        cat "$logs/$k.log"
    fi
done
exit $status
