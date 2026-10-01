#!/usr/bin/env bash
# Builds the redistributable OpenSE4 packages (docs/BUILDING.md, "Release packages"):
#
#   dist/OpenSE4-<version>-linux-x86_64.tar.gz        static except the C library (glibc 2.34+)
#   dist/OpenSE4-<version>-windows-x86_64.zip         static, cross-built with MinGW-w64
#   dist/OpenSE4-<version>-windows-x86_64-setup.exe   the same, as an installer
#   dist/OpenSE4-<version>-SHA256SUMS.txt
#
# On Linux both are built (Windows cross-compiled with MinGW-w64). In MSYS2 on
# Windows (the UCRT64 shell) the Windows packages are built natively instead.
#
# Each holds the game, the dedicated server and the data checker, with our own
# fonts built in, plus the README, the licence (GPL 3.0 or later) and the
# third-party notices. Nothing from the original game is included: players point
# the game at their own installed copy.
#
# The Linux package also carries the desktop entry, icons and AppStream metadata
# (packaging/linux) and install-desktop-entry.sh, which adds the game to the
# desktop's application list. The Windows installer is built with NSIS
# (packaging/windows/opense4.nsi): a makensis on the PATH when there is one, else
# the official Windows build under Wine, downloaded once into build/_tools (or
# taken from $NSIS_DIR).
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

# Native Windows: MSYS2 (MinGW-w64) builds only the Windows packages.
native_windows=0
case "$(uname -s)" in MINGW* | MSYS* | CYGWIN*) native_windows=1 ;; esac
if [ "$native_windows" = 1 ]; then
    [ ${#targets[@]} -gt 0 ] || targets=(windows)
    if [[ " ${targets[*]} " == *" linux "* ]]; then
        echo "The Linux package is built on Linux." >&2
        exit 2
    fi
fi
[ ${#targets[@]} -gt 0 ] || targets=(linux windows)

# A tagged commit (v0.1.0) is packaged as that version; anything else as the
# project version plus the commit.
project_version=$(sed -n 's/^ *VERSION \([0-9.]*\)$/\1/p' CMakeLists.txt | head -1)
if tag=$(git describe --tags --exact-match --match 'v*' 2>/dev/null); then
    version="${tag#v}"
else
    version="${project_version}-$(git rev-parse --short HEAD)"
fi
dist="$root/dist"
mkdir -p "$dist"

app_id=io.github.lowlevelmetal.OpenSE4
metainfo="packaging/linux/$app_id.metainfo.xml"
if [[ "$version" != *-* ]] && ! grep -q "<release version=\"$version\"" "$metainfo"; then
    echo "$metainfo has no <release> entry for $version: add one first." >&2
    exit 1
fi

nsis_version=3.13
nsis_sha256=ba63dffc4410ee89193e1cb5a41989991bd77c61068da17e3156d136b7b0b3d8

installer() {  # installer <staged Windows folder> <output .exe>
    local nsi="$root/packaging/windows/opense4.nsi"
    local defines=(-V2 "-DVERSION=$version" "-DVERSION_NUMBER=$project_version")
    if command -v makensis > /dev/null; then
        if [ "$native_windows" = 1 ]; then
            makensis "${defines[@]}" "-DSTAGE=$(cygpath -w "$1")" "-DOUTFILE=$(cygpath -w "$2")" "$(cygpath -w "$nsi")"
        else
            makensis "${defines[@]}" "-DSTAGE=$1" "-DOUTFILE=$2" "$nsi"
        fi
        return
    fi
    if ! command -v wine > /dev/null; then
        echo "The Windows installer needs NSIS (makensis) or Wine." >&2
        exit 1
    fi
    local nsis="${NSIS_DIR:-$root/build/_tools/nsis-$nsis_version}"
    if [ ! -f "$nsis/makensis.exe" ]; then
        echo "    fetching NSIS $nsis_version"
        mkdir -p "$(dirname "$nsis")"
        local zip="$nsis.zip"
        curl -fsSL -o "$zip" "https://downloads.sourceforge.net/project/nsis/NSIS%203/$nsis_version/nsis-$nsis_version.zip"
        echo "$nsis_sha256  $zip" | sha256sum -c --quiet -
        bsdtar -xf "$zip" -C "$(dirname "$nsis")"
        rm -f "$zip"
    fi
    WINEDEBUG=-all wine "$nsis/makensis.exe" "${defines[@]}" "-DSTAGE=$(winepath -w "$1")" \
        "-DOUTFILE=$(winepath -w "$2")" "$(winepath -w "$nsi")"
}

notices() {  # notices <build dir> <target> <output file>
    local deps="$1/_deps" out="$3"
    {
        echo "OpenSE4 third-party notices"
        echo "==========================="
        echo
        echo "OpenSE4 is an open-source engine for Space Empires IV Deluxe, licensed under"
        echo "the GNU General Public License version 3 or later (see LICENSE). It is not"
        echo "affiliated with the game's publishers and contains nothing from the game:"
        echo "it plays with the player's own installed copy."
        echo "The executables include the following third-party software and fonts,"
        echo "each under its own licence."
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
            # Arch's mingw-w64-crt, or MSYS2's crt package.
            local runtime=/usr/share/licenses/mingw-w64-crt/COPYING.MinGW-w64-runtime.txt
            [ "$native_windows" = 1 ] && runtime="$MINGW_PREFIX/share/licenses/crt/COPYING.MinGW-w64-runtime.txt"
            section "MinGW-w64 runtime" "$runtime"
        fi
        echo; echo; echo "------------------------------------------------------------------------"
        echo "GCC runtime libraries"; echo "------------------------------------------------------------------------"; echo
        echo "The C++ standard library and GCC support libraries are linked in under the"
        echo "GCC Runtime Library Exception (GPL version 3 with the exception)."
    } > "$out"
}

for target in "${targets[@]}"; do
    preset="dist-$target"
    [ "$native_windows" = 1 ] && preset=dist-mingw
    build="$root/build/$preset"
    echo "==> $target: configure and build ($preset)"
    cmake --preset "$preset" > /dev/null
    cmake --build --preset "$preset"

    exe=""
    strip=strip
    if [ "$target" = windows ]; then
        exe=".exe"
        [ "$native_windows" = 1 ] || strip=x86_64-w64-mingw32-strip
    fi

    if [ "$tests" = 1 ]; then
        echo "==> $target: tests"
        if [ "$target" = linux ]; then
            "$build/tests/opense4_tests"
            tools/check_glibc.sh "$build/opense4" "$build/opense4-server" "$build/opense4-datacheck"
        elif [ "$native_windows" = 1 ]; then
            "$build/tests/opense4_tests.exe"
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
    if [ "$target" = linux ]; then
        mkdir -p "$stage/share/applications" "$stage/share/metainfo" "$stage/share/icons"
        cp "packaging/linux/$app_id.desktop" "$stage/share/applications/"
        cp "$metainfo" "$stage/share/metainfo/"
        cp -r packaging/linux/icons/hicolor "$stage/share/icons/"
        cp packaging/linux/install-desktop-entry.sh "$stage/"
        if command -v desktop-file-validate > /dev/null; then
            desktop-file-validate "$stage/share/applications/$app_id.desktop"
        fi
        if command -v appstreamcli > /dev/null; then
            appstreamcli validate --no-net "$stage/share/metainfo/$app_id.metainfo.xml" > /dev/null
        fi
    fi
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
        echo "==> $target: installer"
        rm -f "$dist/$name-setup.exe"
        installer "$stage" "$dist/$name-setup.exe"
        echo "    $dist/$name-setup.exe"
    fi
done

# Checksums for whatever this version's archives are in dist/.
(cd "$dist" && sha256sum OpenSE4-"${version}"-*.tar.gz OpenSE4-"${version}"-*.zip OpenSE4-"${version}"-*-setup.exe 2> /dev/null > "OpenSE4-${version}-SHA256SUMS.txt" || true)
echo "==> checksums: $dist/OpenSE4-${version}-SHA256SUMS.txt"
