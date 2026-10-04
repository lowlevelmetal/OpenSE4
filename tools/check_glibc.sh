#!/usr/bin/env bash
# Reports the newest glibc symbol version each Linux binary needs, and fails when
# one needs more than the release floor (cmake/GlibcCompat.cmake). Any
# architecture: readelf reads every ELF file, whatever machine built it.
#
#   tools/check_glibc.sh [--max=2.34] BINARY...

set -euo pipefail

max="2.34"
bins=()
for arg in "$@"; do
    case "$arg" in
        --max=*) max="${arg#--max=}" ;;
        *) bins+=("$arg") ;;
    esac
done
[ ${#bins[@]} -gt 0 ] || { echo "usage: $0 [--max=2.34] BINARY..." >&2; exit 2; }

newer() {  # is version $1 newer than $2?
    [ "$1" != "$2" ] && [ "$(printf '%s\n%s\n' "$1" "$2" | sort -V | tail -1)" = "$1" ]
}

status=0
for bin in "${bins[@]}"; do
    syms=$(readelf --dyn-syms --wide "$bin")
    need=$(echo "$syms" | grep -o '@GLIBC_[0-9.]*' | sed 's/@GLIBC_//' | sort -V | tail -1)
    if newer "$need" "$max"; then
        echo "$bin: needs glibc $need (more than $max):"
        echo "$syms" | grep -o "[^ ]*@GLIBC_$need\( \|\$\)" | sed 's/ *$//; s/^/    /' | sort -u
        status=1
    else
        echo "$bin: needs glibc $need"
    fi
done
exit $status
