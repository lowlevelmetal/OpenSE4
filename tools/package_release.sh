#!/usr/bin/env bash
# Builds the redistributable OpenSE4 packages (docs/BUILDING.md, "Release packages"):
#
#   dist/OpenSE4-<version>-linux-x86_64.tar.gz   static except the C library (glibc 2.34+)
#   dist/OpenSE4-<version>-windows-x86_64.zip    static, cross-built with MinGW-w64
#
# Each holds the game, the dedicated server and the data checker, with our own
# fonts and prototype data built in, plus the README, the third-party notices and
# the licence once the project has one. Nothing from the original game is included:
# players point the game at their own installed copy.
#
#   tools/package_release.sh [linux] [windows] [--skip-tests]

set -euo pipefail

root=$(cd "$(dirname "$0")/.." && pwd)
cd "$root"

targets=()
tests=1
for arg in "$@"; do
    case "$arg" in
        linux|windows) targets+=("$arg") ;;
        --skip-tests) tests=0 ;;
        *) echo "usage: $0 [linux] [windows] [--skip-tests]" >&2; exit 2 ;;
    esac
done
[ ${#targets[@]} -gt 0 ] || targets=(linux windows)

project_version=$(sed -n 's/^ *VERSION \([0-9.]*\)$/\1/p' CMakeLists.txt | head -1)
version="${project_version}-$(git rev-parse --short HEAD)"
dist="$root/dist"
mkdir -p "$dist"

notices() {  # notices <build dir> <target> <output file>
    local deps="$1/_deps" out="$3"
    {
        echo "OpenSE4 third-party notices"
        echo "==========================="
        echo
        echo "OpenSE4 is an open-source engine for Space Empires IV Deluxe. It is not"
        echo "affiliated with the game's publishers and contains nothing from the game."
        echo "The executables include the following third-party software and fonts."
        section() {
            echo; echo; echo "------------------------------------------------------------------------"
            echo "$1"; echo "------------------------------------------------------------------------"; echo
            cat "$2"
        }
        section "SDL 3 (zlib licence)" "$deps/sdl3-src/LICENSE.txt"
        section "Dear ImGui (MIT licence)" "$deps/imgui-src/LICENSE.txt"
        section "volk (MIT licence)" "$deps/volk-src/LICENSE.md"
        section "Vulkan Memory Allocator (MIT licence)" "$deps/vma-src/LICENSE.txt"
        section "Vulkan headers (Apache 2.0 or MIT)" "$deps/vulkanheaders-src/LICENSE.md"
        section "toml++ (MIT licence)" "$deps/tomlplusplus-src/LICENSE"
        section "stb (MIT licence or public domain)" "$deps/stb-src/LICENSE"
        section "dr_libs (public domain or MIT No Attribution)" "$deps/drlibs-src/LICENSE"
        section "miniupnpc (BSD 3-clause licence)" "$deps/miniupnpc-src/LICENSE"
        echo; echo; echo "------------------------------------------------------------------------"
        echo "Khronos OpenGL headers (MIT licence)"; echo "------------------------------------------------------------------------"; echo
        sed -n '/Copyright/,/\*\//p' third_party/khronos/GL/glcorearb.h
        section "Noto Sans fonts (SIL Open Font License 1.1)" "assets/fonts/OFL.txt"
        if [ "$2" = windows ]; then
            section "MinGW-w64 runtime" "/usr/share/licenses/mingw-w64-crt/COPYING.MinGW-w64-runtime.txt"
        fi
        echo; echo; echo "------------------------------------------------------------------------"
        echo "GCC runtime libraries"; echo "------------------------------------------------------------------------"; echo
        echo "The C++ standard library and GCC support libraries are linked in under the"
        echo "GCC Runtime Library Exception (GPL version 3 with the exception)."
    } > "$out"
}

for target in "${targets[@]}"; do
    preset="dist-$target"
    build="$root/build/$preset"
    echo "==> $target: configure and build ($preset)"
    cmake --preset "$preset" > /dev/null
    cmake --build --preset "$preset"

    exe=""
    strip=strip
    if [ "$target" = windows ]; then exe=".exe"; strip=x86_64-w64-mingw32-strip; fi

    if [ "$tests" = 1 ]; then
        echo "==> $target: tests"
        if [ "$target" = linux ]; then
            "$build/tests/opense4_tests"
            tools/check_glibc.sh "$build/opense4" "$build/opense4-server" "$build/opense4-datacheck"
        elif command -v wine > /dev/null; then
            WINEDEBUG=-all wine "$build/tests/opense4_tests.exe"
        else
            echo "    (Wine not installed: Windows tests skipped)"
        fi
    fi

    name="OpenSE4-${version}-${target}-x86_64"
    stage="$dist/$name"
    rm -rf "$stage"
    mkdir -p "$stage"
    for bin in opense4 opense4-server opense4-datacheck; do
        cp "$build/$bin$exe" "$stage/"
        "$strip" "$stage/$bin$exe"
    done
    cp README.md "$stage/README.md"
    [ -f LICENSE ] && cp LICENSE "$stage/LICENSE"
    notices "$build" "$target" "$stage/THIRD_PARTY_NOTICES.txt"
    if [ "$target" = windows ]; then
        # Windows editors expect CRLF line endings in plain text files.
        for f in "$stage"/*.md "$stage"/*.txt "$stage"/LICENSE; do
            [ -f "$f" ] && sed -i 's/\r*$/\r/' "$f"
        done
    fi

    echo "==> $target: archive"
    rm -f "$dist/$name".tar.gz "$dist/$name".zip
    if [ "$target" = linux ]; then
        tar -C "$dist" -czf "$dist/$name.tar.gz" "$name"
        echo "    $dist/$name.tar.gz"
    else
        (cd "$dist" && bsdtar -a -cf "$name.zip" "$name")
        echo "    $dist/$name.zip"
    fi
done
