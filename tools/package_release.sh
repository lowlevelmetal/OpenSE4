#!/usr/bin/env bash
# Builds the redistributable OpenSE4 packages (docs/BUILDING.md, "Release packages"):
#
#   dist/OpenSE4-<version>-linux-x86_64.tar.gz        static except the C library (glibc 2.34+)
#   dist/OpenSE4-<version>-linux-aarch64.tar.gz       the same for 64-bit ARM
#   dist/OpenSE4-<version>-linux-armhf.tar.gz         the same for 32-bit ARM (ARMv7, hard-float, NEON)
#   dist/OpenSE4-<version>-windows-x86_64.zip         static, Windows 7 SP1 to 11 (llvm-mingw, msvcrt.dll)
#   dist/OpenSE4-<version>-windows-x86_64-setup.exe   the same, as an installer
#   dist/OpenSE4-<version>-SHA256SUMS.txt
#
# On Linux both are built (Windows cross-compiled). In MSYS2 on Windows the
# Windows packages are built natively instead. Either way the Windows programs
# come from the dist-windows preset: llvm-mingw for msvcrt.dll, fetched into
# build/_tools at a pinned version (cmake/toolchains/llvm-mingw-x86_64.cmake),
# and tools/check_windows_imports.py checks that they load on Windows 7 SP1.
#
# `linux` is the Linux package for this machine's own architecture. The ARM
# packages are cross-built on another machine with `linux-aarch64` and
# `linux-armhf` (the dist-linux-aarch64 and dist-linux-armhf presets, GCC's
# aarch64-linux-gnu and arm-linux-gnueabihf cross compilers), their tests run
# through QEMU when it is installed (docs/BUILDING.md, "ARM Linux"). CI builds
# all of them (.github/workflows/release.yml).
#
# Each holds the game, the dedicated server and the data checker, with our own
# fonts built in, plus the README, the licence (GPL 3.0 or later) and the
# third-party notices, and the modding SDK's documentation and example mods in
# sdk/ beside opense4-sdk (sdk/docs from docs/sdk, sdk/examples from mods/examples).
# Nothing from the original game is included: players point the game at their own
# installed copy.
#
# The Linux package also carries the desktop entry, icons and AppStream metadata
# (packaging/linux) and install-desktop-entry.sh, which adds the game to the
# desktop's application list. The Windows installer is built with NSIS
# (packaging/windows/opense4.nsi): a makensis on the PATH when there is one, else
# the official Windows build under Wine, downloaded once into build/_tools (or
# taken from $NSIS_DIR).
#
#   tools/package_release.sh [linux] [linux-aarch64] [linux-armhf] [windows] [--skip-tests]

set -euo pipefail

root=$(cd "$(dirname "$0")/.." && pwd)
cd "$root"

targets=()
tests=1
for arg in "$@"; do
    case "$arg" in
        linux|linux-x86_64|linux-aarch64|linux-armhf|windows) targets+=("$arg") ;;
        --skip-tests) tests=0 ;;
        *) echo "usage: $0 [linux] [linux-aarch64] [linux-armhf] [windows] [--skip-tests]" >&2; exit 2 ;;
    esac
done

# Native Windows: MSYS2 (MinGW-w64) builds only the Windows packages.
native_windows=0
case "$(uname -s)" in MINGW* | MSYS* | CYGWIN*) native_windows=1 ;; esac
if [ "$native_windows" = 1 ]; then
    [ ${#targets[@]} -gt 0 ] || targets=(windows)
    if [[ " ${targets[*]} " == *" linux"* ]]; then
        echo "The Linux package is built on Linux." >&2
        exit 2
    fi
fi
[ ${#targets[@]} -gt 0 ] || targets=(linux windows)

# Linux packages are named after the architecture, as Debian names it for ARM:
# x86_64, aarch64 or armhf. `linux` is this machine's own; another one is a
# cross build.
host_arch=""
if [ "$native_windows" = 0 ]; then
    case "$(${CC:-cc} -dumpmachine 2> /dev/null || uname -m)" in
        x86_64*) host_arch=x86_64 ;;
        aarch64*) host_arch=aarch64 ;;
        arm*-*gnueabihf | armv7* | armv8l) host_arch=armhf ;;
    esac
fi
for i in "${!targets[@]}"; do
    if [ "${targets[$i]}" = linux ]; then
        if [ -z "$host_arch" ]; then
            echo "No Linux package for this machine's architecture: use linux-aarch64 or linux-armhf." >&2
            exit 2
        fi
        targets[i]="linux-$host_arch"
    fi
done

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

# A Linux package's programs link only the C library: SDL loads the rest at run
# time, by the names below (the windowing systems, GPU APIs and sound servers).
# A cross build without the target's development packages would silently leave
# a backend out.
check_linux_libraries() {  # check_linux_libraries <build dir>
    local bin lib status=0
    for bin in "$1/opense4" "$1/opense4-server" "$1/opense4-datacheck" "$1/opense4-convert" "$1/opense4-sdk"; do
        for lib in $(readelf -d "$bin" | sed -n 's/.*(NEEDED).*\[\(.*\)\]/\1/p'); do
            case "$lib" in
                libc.so.6 | libm.so.6 | ld-linux*.so.*) ;;
                *) echo "$bin links $lib: only the C library may be linked" >&2; status=1 ;;
            esac
        done
    done
    local loads
    loads=$(strings "$1/opense4")
    for lib in libX11.so.6 libwayland-client.so.0 libxkbcommon.so.0 libdecor-0.so.0 libvulkan.so.1 libGL.so.1 libEGL.so.1 \
               libasound.so.2 libpulse.so.0 libpipewire-0.3.so.0 libudev.so.1; do
        if ! grep -qx "$lib" <<< "$loads"; then
            echo "$1/opense4 does not load $lib: SDL was built without it" >&2
            status=1
        fi
    done
    [ "$status" = 0 ] && echo "    links only the C library; SDL loads X11, Wayland, Vulkan, OpenGL and the sound servers at run time"
    return "$status"
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
        section "Monocypher (BSD 2-clause licence or CC0 1.0)" "$deps/monocypher-src/LICENCE.md"
        section "miniz (MIT licence)" "$deps/miniz-src/LICENSE"
        section "MicroPython (MIT licence)" "third_party/micropython/LICENSE"
        section "MicroPython's double-precision math functions, from musl (MIT licence)" "third_party/micropython/lib/libm_dbl/README"
        section "re1.5, MicroPython's regular expression engine (BSD 3-clause licence)" "third_party/micropython/licenses/re1.5.txt"
        echo; echo; echo "------------------------------------------------------------------------"
        echo "Khronos OpenGL headers (MIT licence)"; echo "------------------------------------------------------------------------"; echo
        sed -n '/Copyright/,/\*\//p' third_party/khronos/GL/glcorearb.h
        section "Noto Sans fonts (SIL Open Font License 1.1)" "assets/fonts/OFL.txt"
        if [ "$2" = windows ]; then
            # The runtime of the toolchain (llvm-mingw): MinGW-w64's, and LLVM's
            # C++ library, unwinder and compiler runtime.
            local toolchain
            toolchain=$(sed -n 's/^LLVM_MINGW_DIR:PATH=//p' "$1/CMakeCache.txt")
            section "MinGW-w64 runtime" "$toolchain/x86_64-w64-mingw32/share/mingw32/COPYING.MinGW-w64-runtime.txt"
            section "MinGW-w64 winpthreads" "$toolchain/x86_64-w64-mingw32/share/mingw32/COPYING.winpthreads.txt"
            section "LLVM runtime libraries: libc++, libc++abi, libunwind, compiler-rt (Apache 2.0 with LLVM exceptions)" \
                "$toolchain/LICENSE.TXT"
        else
            echo; echo; echo "------------------------------------------------------------------------"
            echo "GCC runtime libraries"; echo "------------------------------------------------------------------------"; echo
            echo "The C++ standard library and GCC support libraries are linked in under the"
            echo "GCC Runtime Library Exception (GPL version 3 with the exception)."
        fi
    } > "$out"
}

for target in "${targets[@]}"; do
    exe=""
    strip=strip
    run=()      # how the tests run: directly, or through QEMU
    case "$target" in
        linux-*)
            arch="${target#linux-}"
            platform=linux
            preset=dist-linux
            if [ "$arch" != "$host_arch" ]; then
                case "$arch" in
                    aarch64) triple=aarch64-linux-gnu qemu=qemu-aarch64 ;;
                    armhf) triple=arm-linux-gnueabihf qemu=qemu-arm ;;
                    *) echo "The $arch package is built on an $arch machine." >&2; exit 2 ;;
                esac
                preset="dist-$target"
                strip="$triple-strip"
                if emulator=$(command -v "$qemu" || command -v "$qemu-static"); then
                    run=("$emulator" -L "/usr/$triple")
                else
                    run=(false)
                fi
            fi
            ;;
        windows)
            arch=x86_64
            platform=windows
            preset=dist-windows
            exe=".exe"
            ;;
    esac
    build="$root/build/$preset"
    echo "==> $target: configure and build ($preset)"
    cmake --preset "$preset" > /dev/null
    cmake --build --preset "$preset"

    if [ "$platform" = windows ]; then
        strip=$(sed -n 's/^CMAKE_STRIP:FILEPATH=//p' "$build/CMakeCache.txt")  # the toolchain's
        echo "==> $target: imports (Windows 7 SP1)"
        python3 tools/check_windows_imports.py "$build/opense4.exe" "$build/opense4-server.exe" \
            "$build/opense4-datacheck.exe" "$build/opense4-convert.exe" "$build/opense4-sdk.exe" "$build/tests/opense4_tests.exe"
    fi

    if [ "$tests" = 1 ]; then
        echo "==> $target: tests"
        if [ "$platform" = linux ]; then
            if [ "${run[0]:-}" = false ]; then
                echo "    ($qemu not installed: $arch tests skipped)"
            else
                "${run[@]}" "$build/tests/opense4_tests"
            fi
            tools/check_glibc.sh "$build/opense4" "$build/opense4-server" "$build/opense4-datacheck" "$build/opense4-convert" "$build/opense4-sdk"
            check_linux_libraries "$build"
        elif [ "$native_windows" = 1 ]; then
            "$build/tests/opense4_tests.exe"
        elif command -v wine > /dev/null; then
            # In a Wine prefix of its own that reports Windows 7 SP1. Wine has the
            # newer functions whatever it reports: the import check above is what
            # holds the programs to Windows 7.
            (
                export WINEPREFIX="$root/build/_tools/wine-win7" WINEDEBUG=-all \
                    WINEDLLOVERRIDES="winemenubuilder.exe=d;mscoree=d;mshtml=d"
                if [ ! -f "$WINEPREFIX/system.reg" ]; then
                    wine wineboot -i > /dev/null 2>&1
                    wine winecfg -v win7 > /dev/null 2>&1
                    if command -v wineserver > /dev/null; then wineserver -w; fi
                fi
                wine "$build/tests/opense4_tests.exe"
            )
        else
            echo "    (Wine not installed: Windows tests skipped)"
        fi
    fi

    name="OpenSE4-${version}-${platform}-${arch}"
    stage="$dist/$name"
    rm -rf "$stage"
    mkdir -p "$stage"
    for bin in opense4 opense4-server opense4-datacheck opense4-convert opense4-sdk; do
        cp "$build/$bin$exe" "$stage/"
        "$strip" "$stage/$bin$exe"
    done
    cp README.md "$stage/README.md"
    [ -f LICENSE ] && cp LICENSE "$stage/LICENSE"
    # The modding SDK's guide, reference and example mods, beside opense4-sdk, which
    # finds the examples there (opense4-sdk new --from-example).
    mkdir -p "$stage/sdk"
    cp -r docs/sdk "$stage/sdk/docs"
    cp -r mods/examples "$stage/sdk/examples"
    find "$stage/sdk" -name __pycache__ -type d -prune -exec rm -rf {} +
    notices "$build" "$platform" "$stage/THIRD_PARTY_NOTICES.txt"
    if [ "$platform" = linux ]; then
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
    if [ "$platform" = linux ]; then
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
