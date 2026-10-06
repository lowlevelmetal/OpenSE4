# Building OpenSE4

OpenSE4 is plain CMake. It builds with GCC, Clang, Apple Clang and MSVC on Linux, Windows
and macOS. Linux is the primary development platform. Windows builds are routine. CI
builds and tests macOS as well, but the game itself is played there less. On Linux it
builds for x86_64 and for ARM, 64-bit (aarch64) and 32-bit (armhf), natively or
cross-compiled (see "ARM Linux").

What gets built:

| Target | What it is |
|---|---|
| `opense4` | The game client: Vulkan 1.3 with an OpenGL 3.3 fallback |
| `opense4-server` | Dedicated multiplayer host, and the PBEM turn processor |
| `opense4-datacheck` | Loads and validates an installed or modded classic data set |
| `opense4-convert` | Converts saved games between the original's format and OpenSE4's, and describes the original's (see "Saved games of the original") |
| `opense4-observe` | Linux-only harness for observing the original game (see docs/CLEANROOM.md) |
| `opense4_tests` | Unit tests. They use only our own fixtures |

## Requirements

- A C++23 compiler: GCC 14+, Clang 18+, Visual Studio 2022 17.10+ or 2026, or on macOS
  Apple Clang 21 (Xcode 26.6).
- CMake 3.25+ and Ninja. Visual Studio's own generator also works on Windows.
- SDL3 3.2+. A system copy is used if found; otherwise CMake fetches and builds it.
- `glslc` to compile the shaders at build time. It comes with the Vulkan SDK or the
  `shaderc` package.
- Vulkan headers. A system copy is used if found; otherwise they are fetched.

CMake fetches the remaining dependencies at pinned versions with verified hashes:

- Dear ImGui, volk, VMA, toml++, stb, dr_mp3 (music) and doctest;
- Monocypher, the cryptography of encrypted network games and signed e-mail orders;
- miniupnpc, for automatic router port forwarding.

The first configure therefore needs network access. Later builds do not.

MicroPython, the interpreter of the modding SDK's scripts, is part of the source tree
(`third_party/micropython`, see "MicroPython"): nothing to install or fetch.

The Vulkan loader is **not** linked. It is loaded at runtime, so the game still starts,
on OpenGL, on machines without Vulkan.

## Linux

Install the tools and libraries:

| Distribution | Command |
|---|---|
| Arch | `sudo pacman -S base-devel cmake ninja sdl3 shaderc vulkan-headers` |
| Fedora 41+ | `sudo dnf install gcc-c++ cmake ninja-build SDL3-devel glslc vulkan-headers` |
| Debian 13 / Ubuntu 25.04+ | `sudo apt install build-essential cmake ninja-build libsdl3-dev glslc libvulkan-dev` |
| openSUSE Tumbleweed | `sudo zypper install gcc-c++ cmake ninja SDL3-devel shaderc vulkan-headers` |

Older distributions without SDL3 packages still work, because CMake then fetches SDL3.
SDL's own build needs the usual X11/Wayland development headers; see SDL's
`docs/README-linux.md`.

For `--validation`, also install the Vulkan validation layers
(`vulkan-validation-layers` on most distributions).

Then build:

```sh
cmake --preset debug          # or: release, asan
cmake --build --preset debug
ctest --preset debug          # or run ./build/debug/tests/opense4_tests directly
./build/debug/opense4
```

## Windows

1. Install Visual Studio 2022 or 2026 with **Desktop development with C++**. It
   includes CMake and Ninja.
2. Install the [Vulkan SDK](https://vulkan.lunarg.com/sdk/home#windows), which
   provides `glslc` and the headers. The installer sets `VULKAN_SDK`, and the build
   looks for `glslc` there.
3. Open **x64 Native Tools Command Prompt** for your Visual Studio and run:

```bat
cmake --preset release
cmake --build --preset release
build\release\opense4.exe
```

SDL3 is fetched automatically unless CMake can find an installed copy. Pass
`-DCMAKE_PREFIX_PATH=C:\path\to\SDL3` to use a prebuilt SDL3. The SDL3 DLL is copied
next to the executable.

An MSVC build links Microsoft's C and C++ runtime as DLLs (`vcruntime140.dll`,
`msvcp140.dll` and the Universal C Runtime). The computer it runs on needs the Visual
C++ Redistributable, and on Windows 7 also the Universal C Runtime update (KB2999226).
Use it for development. The release packages come from the `dist-windows` preset, and
they need nothing installed (see "Windows 7 to 11").

You can also open the source folder directly in Visual Studio ("Open Folder"), which
reads `CMakePresets.json`.

Every Windows program carries an application manifest
(`packaging/windows/opense4.manifest`). It names Windows 7 to 11 as supported and sets
the UTF-8 code page, so that file names with letters outside ASCII work as on Linux.
Windows 10 1903 and later honour the code page. On older Windows the release build
reads paths, environment variables and command lines as UTF-8 itself (see "Windows 7
to 11"); an MSVC build uses the system's code page there. `.gitattributes` turns off
line-end conversion, so a clone with `core.autocrlf` builds the same program as on
Linux.

## macOS

Apple Clang and libc++ come with Xcode or its Command Line Tools
(`xcode-select --install`); the rest comes from [Homebrew](https://brew.sh):

```sh
brew install cmake ninja sdl3 shaderc vulkan-headers
cmake --preset debug          # or: release
cmake --build --preset debug
./build/debug/tests/opense4_tests
./build/debug/opense4 --renderer=opengl
```

CI does the same for every push on Apple silicon (the `macos-latest` runner: macOS 26,
Xcode 26.6, Apple Clang 21): the `debug` preset with warnings as errors, the unit tests
with the determinism goldens among them, and `--help` of the programs. It never opens
the game's window: the game itself is not tested on macOS.

Use the OpenGL renderer: macOS provides OpenGL up to 4.1, more than the 3.3 core
profile the game needs. Vulkan would run through MoltenVK (`brew install molten-vk`),
which is optional and untested; when Vulkan does not start, the default
`--renderer=auto` falls back to OpenGL.

## ARM Linux

OpenSE4 builds and plays the same on 64-bit ARM (aarch64) and 32-bit ARM (armhf) Linux:
Raspberry Pi OS, Debian, Ubuntu or Fedora Asahi Remix, on a Raspberry Pi, a Rockchip
board, an Apple silicon Mac or a Snapdragon laptop. The README's "System requirements"
lists the GPUs that can run the game.

**On the ARM machine itself**, build as on any Linux (see "Linux"): the same packages,
presets and commands. Raspberry Pi OS 13 has what the table's Debian 13 line installs.
A 32-bit build gets 64-bit `time_t` and file offsets on its own (`_TIME_BITS=64`,
`_FILE_OFFSET_BITS=64`, from `CMakeLists.txt`).

**Cross-compiling** from x86_64 uses a toolchain file:

| Preset | Toolchain file | Target |
|---|---|---|
| `dist-linux-aarch64` | `cmake/toolchains/aarch64-linux-gnu.cmake` | 64-bit ARM (ARMv8-A) |
| `dist-linux-armhf` | `cmake/toolchains/arm-linux-gnueabihf.cmake` | 32-bit ARM: ARMv7-A, hard-float, VFPv3 and NEON |

Both use GCC's `aarch64-linux-gnu-` or `arm-linux-gnueabihf-` cross compiler (Debian and
Ubuntu: `g++-aarch64-linux-gnu`, `g++-arm-linux-gnueabihf`; Arch: `aarch64-linux-gnu-gcc`)
and find the target's libraries in this order (`cmake/toolchains/linux-cross.cmake`):

1. a sysroot given with `-DCMAKE_SYSROOT=/path`;
2. Debian's and Ubuntu's multiarch folders, `/usr/lib/<triple>`: the target's packages
   installed next to the machine's own (`dpkg --add-architecture armhf`, then
   `apt install libx11-dev:armhf ...`; Ubuntu serves ARM packages from its ports
   archive, see `.github/workflows/release.yml`);
3. the cross compiler's own folder, `/usr/<triple>` (Arch, Fedora).

`pkg-config` sees only the target's files. A release build needs SDL's build
dependencies for the target (the list in `release.yml`); without X11 and Wayland, SDL's
configure stops. A build only for the tests can do without them:
`-DSDL_UNIX_CONSOLE_BUILD=ON` builds SDL without windows, as CI's armhf job does.

Clang works too: give it the target and a sysroot, for example one unpacked from the
distribution's packages:

```sh
cmake --preset release -B build/armhf --toolchain cmake/toolchains/arm-linux-gnueabihf.cmake \
    -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
    -DCMAKE_C_COMPILER_TARGET=arm-linux-gnueabihf -DCMAKE_CXX_COMPILER_TARGET=arm-linux-gnueabihf \
    -DCMAKE_SYSROOT=/path/to/armhf-sysroot -DCMAKE_LINKER_TYPE=LLD -DSDL_UNIX_CONSOLE_BUILD=ON
```

**Tests** of a cross build run through QEMU's user mode (`qemu-aarch64`, `qemu-arm`, or
their `-static` builds; Debian and Ubuntu: `qemu-user`). The toolchain file makes it
CMake's emulator, so `ctest` uses it; by hand, `-L` names the target's C library:

```sh
qemu-arm -L /usr/arm-linux-gnueabihf build/dist-linux-armhf/tests/opense4_tests
```

Under QEMU an optimized build runs the suite much faster than a debug build: about a
minute and a half in four processes (`.github/scripts/run_tests_parallel.sh`) on a
desktop machine.

**The armhf baseline**, ARMv7-A with VFPv3 and NEON, covers every Raspberry Pi from the
Pi 2 on and the ARMv7 and ARMv8 boards that run a 32-bit system. Raspberry Pi OS
(32-bit) runs it, though its own packages are built for the ARMv6 of the Pi 1 and Pi Zero,
which are left out. VFPv3 rather than VFPv4 keeps the Cortex-A8 and A9 in; VFPv4 would
add only fused multiply-add, which the game never uses.

**The same game:** the golden checksums of `test_determinism.cpp` are the same on
aarch64 and armhf as on x86_64, and CI checks them on both (docs/ENGINE.md, "Same on
every platform"). Two things keep it so. Sizes are hashed and stored as 64-bit values,
since `size_t` is 32 bits on armhf. And GCC and Clang build with `-ffp-contract=off`:
ARM has fused multiply-add, which GCC would otherwise use for `a * b + c` in optimized
code, rounding once where x86_64 rounds twice. The engine computes almost nothing in
floating point, but Dear ImGui's layout does (an aarch64 build without the option had
hundreds of fused instructions in it).

## Presets and options

| Preset | Build type | Notes |
|---|---|---|
| `debug` | Debug | The default for development |
| `release` | RelWithDebInfo | Optimized, with symbols |
| `asan` | Debug | AddressSanitizer and UndefinedBehaviorSanitizer (GCC and Clang) |
| `dist-linux` | Release | Redistributable Linux build for this machine's architecture (see "Release packages") |
| `dist-linux-aarch64` | Release | The same for 64-bit ARM, cross-compiled (see "ARM Linux") |
| `dist-linux-armhf` | Release | The same for 32-bit ARM (ARMv7, hard-float, NEON), cross-compiled |
| `dist-windows` | Release | Redistributable Windows build for Windows 7 SP1 to 11: llvm-mingw for `msvcrt.dll`, fetched at a pinned version (see "Windows 7 to 11"). A cross build on Linux, or native on Windows |
| `dist-mingw` | Release | A Windows build in MSYS2 with its own MinGW-w64 GCC. It needs the Universal C Runtime (Windows 10 and later, or Windows 7 with KB2999226), so the packages do not use it |

| CMake option | Default | Effect |
|---|---|---|
| `OPENSE4_BUILD_TESTS` | ON | Build `opense4_tests` and fetch doctest |
| `OPENSE4_WARNINGS_AS_ERRORS` | OFF | Treat warnings as errors (CI uses this) |
| `OPENSE4_ENABLE_UPNP` | ON | Build with miniupnpc. OFF compiles a no-op port mapper |
| `OPENSE4_STATIC` | OFF | Link statically for redistribution (the `dist-*` presets) |
| `OPENSE4_EMBED_RESOURCES` | ON | Build our fonts into the executable; files on disk still win |
| `OPENSE4_DEV_PATHS` | ON | Let the client find `assets/` in the source tree. The `dist-*` presets turn it off, which also keeps the build machine's paths out of the binaries |

The code must compile without warnings under
`-Wall -Wextra -Wpedantic -Wshadow -Wconversion` (`/W4` on MSVC).

## Release packages

`tools/package_release.sh [linux] [windows]` builds, tests and packages both
platforms from one Linux machine. In MSYS2 on Windows it builds the Windows packages
natively instead. Either way they come from the `dist-windows` preset:

- `dist/OpenSE4-<version>-linux-x86_64.tar.gz`
- `dist/OpenSE4-<version>-linux-aarch64.tar.gz` and `-linux-armhf.tar.gz`, the same for ARM
  (see "The ARM packages")
- `dist/OpenSE4-<version>-windows-x86_64.zip`
- `dist/OpenSE4-<version>-windows-x86_64-setup.exe`, the same programs as an installer
- `dist/OpenSE4-<version>-SHA256SUMS.txt`

Each package holds `opense4`, `opense4-server`, `opense4-datacheck` and `opense4-convert`, stripped,
with the README, `LICENSE` (GPL 3.0 or later) and `THIRD_PARTY_NOTICES.txt`. Our own fonts (Noto Sans, SIL Open
Font License) are built into the game, so nothing else needs to sit next to it.
The Linux package also holds the desktop entry, icons and AppStream metadata under
`share/`, and `install-desktop-entry.sh`, which adds the game to the user's
application list (see "Installing on Linux"). The script refuses to package a tagged
version that has no `<release>` entry in the AppStream metadata.
Nothing from the original game is included: players point OpenSE4 at their own
installed copy, without which the game does not start.

- **Linux** (`dist-linux`, and `dist-linux-aarch64` and `dist-linux-armhf` for ARM):
  SDL3, the C++ runtime and every other library are linked statically. Only the C
  library stays shared, because SDL loads the system's X11/Wayland, audio and GPU
  driver libraries at run time; the script checks both. The binaries run on glibc
  2.34 and later (Ubuntu 22.04, Debian 12, Fedora 35, RHEL 9, SteamOS 3 and newer; on
  ARM also Raspberry Pi OS 12 and Fedora Asahi Remix), even when built on a newer
  distribution: `cmake/GlibcCompat.cmake` routes the few newer glibc functions to older
  versions (each architecture's own: `src/compat/glibc_compat.c`) or small built-in
  implementations. `tools/check_glibc.sh` reports what a binary needs.
- **Windows** (`dist-windows`): built with llvm-mingw for `msvcrt.dll` and linked
  with `-static`, so the C++ runtime (libc++), the unwinder and winpthreads are in
  the executables. They need only DLLs that every Windows from 7 SP1 on has:
  before packaging, `tools/check_windows_imports.py` checks every program's imports
  against Windows 7 SP1 (see "Windows 7 to 11"). The game is a windowed application;
  started from a console it still prints `--help` and its log there. With Wine
  installed, the script runs the Windows tests through it, in a Wine prefix of its own
  (`build/_tools/wine-win7`) that reports Windows 7 SP1.

Requirements beyond a normal build:

- network access the first time (SDL3 is fetched and built as a static library, and
  the Windows toolchain is fetched into `build/_tools`);
- `python3` for the Windows import check;
- `bsdtar` for the zip file;
- NSIS or Wine for the installer (see "The Windows installer"), and Wine to run the
  Windows tests on Linux.

In MSYS2, install `git`, `python` and the UCRT64 packages `cmake`, `ninja`, `shaderc`,
`libarchive` (for `bsdtar`) and `nsis`, each named `mingw-w64-ucrt-x86_64-<name>`. The
compiler is llvm-mingw's Windows build, which the toolchain file fetches; MSYS2's own GCC
would make programs for the Universal C Runtime. CI does not run this native route: it
cross-builds on Linux, as the releases are made.

Pass `--skip-tests` to package without running the tests.

### The ARM packages

`linux` packages the machine's own architecture: on an aarch64 or armhf machine,
`tools/package_release.sh linux` makes that architecture's package with `dist-linux`.
`linux-aarch64` and `linux-armhf` cross-compile on x86_64 with the presets of "ARM
Linux", and run the tests through QEMU when it is installed.

For a release, CI makes them: `.github/workflows/release.yml` builds the aarch64 package
natively on GitHub's arm64 runner and cross-compiles the armhf one, for every version
tag. With the x86_64 and Windows packages made here, add them to `dist/`:

```sh
git push origin v0.9.0                    # the release workflow starts
tools/package_release.sh                  # meanwhile: x86_64 Linux and Windows, here
tools/fetch_arm_packages.sh v0.9.0        # the ARM packages of that workflow run
gh release create v0.9.0 dist/OpenSE4-0.9.0-*
```

`tools/fetch_arm_packages.sh` checks that the workflow run for the tag succeeded and
built the commit the tag names, downloads its two ARM packages with the GitHub CLI and
writes `dist/OpenSE4-<version>-SHA256SUMS.txt` again, over every archive of that version.
The run's `OpenSE4-<version>` artifact holds every package with their checksums too.

### The Windows installer

`packaging/windows/opense4.nsi` is an NSIS 3 script for a standard installer
(welcome, licence, components, folder, finish). It installs for all users into
Program Files and adds:

- a Start menu entry and, if chosen, a desktop shortcut;
- an Apps & features entry with the icon, version and an uninstaller.

An update goes into the folder of the previous install. The uninstaller leaves
saved games and settings in `%APPDATA%\OpenSE4` alone. Silent use works as with any
NSIS installer: `setup.exe /S`, and `/D=C:\path` (last, unquoted) for another folder.
On 32-bit Windows, on Windows older than 7, and on Windows 7 without Service Pack 1
the installer says what is missing and stops (exit status 2 when silent).

The script uses `makensis` when it is on the PATH (Debian and Ubuntu package it as
`nsis`, MSYS2 as `mingw-w64-ucrt-x86_64-nsis`). Otherwise it runs the official Windows build of NSIS under Wine. That build
is downloaded once into `build/_tools` and checked against a pinned SHA-256, or
taken from `NSIS_DIR`.

`opense4.exe` carries the icon and version information from
`packaging/windows/opense4.rc.in`. The icon (`opense4.ico`) and the installer's side
picture are rendered from the SVG icon by `tools/render_icons.sh`.

### Windows 7 to 11

The Windows packages run on 64-bit Windows 7 with Service Pack 1, 8, 8.1, 10 and 11,
with nothing else to install. This has been checked by import analysis and under Wine
set to Windows 7 SP1, not on real Windows 7. Wine provides the newer functions whatever
version it reports, and it honours the manifest's UTF-8 code page even as Windows 7, so
a Wine run alone cannot show that a program works on Windows 7.

**The C runtime.** MinGW-w64 toolchains now link the Universal C Runtime
(`api-ms-win-crt-*.dll`, `ucrtbase.dll`). Windows 10 and 11 have it; Windows 7 gets it
only from update KB2999226, which a fresh SP1 lacks, and a program that imports it does
not start there. The choices were:

- ship the Universal C Runtime's DLLs beside the programs: Microsoft allows this, but
  the DLLs come with the Windows SDK, under Microsoft's licence, and add some forty
  files to every package;
- require the update, with the installer checking for it: Windows 7 players then have
  to find and install it before the game starts, and the zip has no installer to
  tell them;
- link `msvcrt.dll`, the C library that every Windows version has.

The `dist-windows` preset takes the last: [llvm-mingw](https://github.com/mstorsjo/llvm-mingw)
(MinGW-w64 15 with Clang, LLD and libc++) in its build for `msvcrt.dll`. Its headers and
runtime libraries target Windows 7. `cmake/toolchains/llvm-mingw-x86_64.cmake` downloads
the pinned release (about 80 MB) into `build/_tools` once and checks its SHA-256, for a
Linux x86_64 or a Windows x64 host; `LLVM_MINGW_DIR` names another copy of the same
build. What this changes:

- The Windows release is built with Clang and libc++ rather than GCC and libstdc++. The
  rules give the same golden checksums with every compiler (docs/ENGINE.md, "Same on
  every platform"); libc++ is also what macOS uses.
- `msvcrt.dll` is an older C library, but MinGW-w64 brings its own C99 `printf` and
  `scanf` families, so numbers print as on Linux.
- libc++ converts between paths and narrow strings in the ANSI code page, which is
  UTF-8 only where the manifest's code page applies (Windows 10 1903 and later).
  `cmake/WindowsCompat.cmake` has the linker send those two conversions to
  `src/compat/libcxx_utf8_paths.cpp`, which uses UTF-8 as libstdc++ does, and
  `src/core/environment.hpp` reads environment variables and the server's and data
  checker's arguments as UTF-8. A user folder such as `C:\Users\José` therefore works on
  every Windows version. `tests/test_platform.cpp` checks this.
- MSYS2 deprecated its own `msvcrt.dll` environment (MINGW64) in 2026, so a native build
  in MSYS2 uses llvm-mingw's Windows build too.

**The import check.** `tools/check_windows_imports.py` reads the import tables of
executables and DLLs (ordinary and delay-loaded) and fails on any DLL that a fresh
Windows 7 SP1 x64 lacks (it keeps a reviewed list of the ones it has) and on any function
of Windows 8 or later, from a documented list per DLL that includes the symbols
`msvcrt.dll` gained after Windows Vista. `tools/package_release.sh` runs it before
packaging, and so does CI. A newer function may still be used through `GetProcAddress`
with a fallback, as SDL3 does for DPI awareness, thread names and the precise clock.

```sh
python3 tools/check_windows_imports.py build/dist-windows/*.exe
python3 tools/check_windows_imports.py --list build/dist-windows/opense4.exe   # every import
python3 tools/check_windows_imports.py --msvc-runtime build/release/*.exe build/release/SDL3.dll
```

`--msvc-runtime` allows the Visual C++ and Universal C runtimes of an MSVC build, which
must then be installed on the computer.

**The rest.**

- Our code and Dear ImGui compile with `_WIN32_WINNT` and `WINVER` set to 0x0601
  (Windows 7), so the Windows headers declare nothing newer; miniupnpc is set to
  Windows 7 too. SDL3 chooses its own version, supports every Windows from XP on, and
  loads newer functions at run time.
- The manifest names Windows 7, 8, 8.1 and 10/11 as supported. SDL declares the DPI
  awareness: per monitor on Windows 8.1 and later, the whole system on 7 and 8, which
  have nothing finer.
- Windows 7 drivers seldom offer Vulkan 1.3, so there the game draws with the OpenGL
  3.3 of the graphics card maker's driver. When neither starts, it says what it needs,
  to install the maker's current driver, and why each renderer failed.
- The network code uses `WSAPoll` (Windows Vista and later) and `BCryptGenRandom` with
  the system's preferred generator (Windows 7 and later).
- The installer checks for Windows 7 SP1 (see "The Windows installer").

## Installing on Linux

`cmake --install` puts the programs in `bin/` and, on Linux, the files that let
desktops list the game (`packaging/linux`):

```sh
cmake --install build/release --prefix ~/.local      # for your user
sudo cmake --install build/release --prefix /usr/local
```

| File | Installed to |
|---|---|
| `io.github.lowlevelmetal.OpenSE4.desktop` | `share/applications/` |
| `io.github.lowlevelmetal.OpenSE4.metainfo.xml` (AppStream, for software centres) | `share/metainfo/` |
| The icon: an SVG, and PNGs from 16 to 512 pixels | `share/icons/hicolor/` |

The game sets `io.github.lowlevelmetal.OpenSE4` as its Wayland app ID and X11 window
class, so the desktop matches its window to the entry. The PNGs are rendered from the
SVG by `tools/render_icons.sh` (needs `rsvg-convert`). Add a `<release>` entry to the
metainfo file for each new version.

For the release package, `install-desktop-entry.sh` does the same for one user. It
copies the entry and icons into `~/.local/share` (or `$XDG_DATA_HOME`), with `Exec`
pointing at the unpacked folder.

## MicroPython

In-game scripts run on MicroPython (docs/sdk/runtime.md). `cmake/MicroPython.cmake`
builds it as a static C library, `opense4_micropython`, from
`third_party/micropython`: the pinned release (1.29.0) with our patches applied and
the headers MicroPython generates for its configuration
(`src/script/port/mpconfigport.h`), all committed. Generating those headers takes
Python, make and a C preprocessor run over MicroPython's sources; doing that at build
time would have to work the same with GCC, Clang, MSVC, llvm-mingw and the ARM cross
compilers, and offline. So it is done once, by `tools/update_micropython.sh`, and
every compiler builds the same files with nothing generated or fetched.

Run the script (on Linux or macOS; it needs curl, make, patch, python3 and cc) after
changing `mpconfigport.h` or a patch in `third_party/micropython/patches/`, or to move
to a new MicroPython release (change its version and SHA-256, then refresh the
patches). It downloads the release once into `build/_tools`, checks its SHA-256,
applies the patches, generates the headers and replaces `third_party/micropython`
(keeping `patches/` and `licenses/`). CMake stops with a message when
`mpconfigport.h` no longer matches the generated headers.

MicroPython is third-party code: our warning flags stay off it, and in the `asan`
preset its undefined-behaviour checks are off (its garbage collector's own stack scan
is exempt from the address checks). Our port (`src/script/port/*.c`) is ours and stays
warning-free. Its licence (MIT), musl's for the bundled math functions (MIT) and
re1.5's for the regular expression engine (BSD 3-clause) go into the release
packages' `THIRD_PARTY_NOTICES.txt`.

## Offline builds

Point FetchContent at already-downloaded sources:

```sh
cmake --preset debug -DFETCHCONTENT_FULLY_DISCONNECTED=ON \
    -DFETCHCONTENT_SOURCE_DIR_IMGUI=/path/to/imgui ...
```

The dependency names are `IMGUI`, `VOLK`, `VMA`, `VULKANHEADERS`, `TOMLPLUSPLUS`,
`STB`, `DRLIBS`, `MONOCYPHER`, `DOCTEST` and `MINIUPNPC`. MicroPython needs nothing:
it is in the source tree. The sources for each dependency are also under
`build/<preset>/_deps/<name>-src` after any online configure. You can reuse them for other build directories or worktrees
with `FETCHCONTENT_SOURCE_DIR_<NAME>`.

## Tests

```sh
./build/debug/tests/opense4_tests                          # everything
./build/debug/tests/opense4_tests -tc="engine*"            # a subset (doctest filters)
OPENSE4_CLASSIC_DATA=auto ./build/debug/tests/opense4_tests  # + checks against your install
```

By default the tests use only the original fixtures in `tests/fixtures/` and
`tests/engine_fixture.cpp`. Setting `OPENSE4_CLASSIC_DATA` to `auto`, or to a data
directory, also runs checks against your installed game data. Those checks never
copy anything into the repository. Each test run uses a scratch user data folder
(`OPENSE4_USER_DIR`), so tests never touch your own settings, saves or history.

`test_xmath.cpp` checks the emulated x87 arithmetic of the rules against exact
arithmetic in multi-word integers on every compiler, and against the x87 itself on x86
with GCC or Clang.

`test_determinism.cpp` compares fixed games and battles with golden checksums, so every
compiler must compute exactly the same game (docs/ENGINE.md, "Same on every
platform"). If a deliberate rules change moves them, print the new values with
`OPENSE4_PRINT_GOLDEN=1 ./build/debug/tests/opense4_tests -tc="determinism*" -s` and
paste them in. A failure on one compiler only means the engine depends on something
that compiler does differently; the message names the first turn and part of the state
that differ.

The input scripts play the client itself through its own input, so they need your
installed game and are opt-in (see "Input scripts" below):

```sh
OPENSE4_CLASSIC_DATA=auto python3 tools/run_input_tests.py       # every script, on your install
OPENSE4_CLASSIC_DATA=auto python3 tools/run_input_tests.py --small   # ... and at 800x600 too
python3 tools/run_input_tests.py --fixture-data --small          # the ones that run on our fixtures, at both layouts (as CI)
OPENSE4_CLASSIC_DATA=auto python3 tools/run_input_tests.py --only-small 'tutorial-*'   # the tutorials at 800x600
```

Each script plays in the layout its `options` give, most at 1024x768. The original's small
800x600 layout differs where lessons notice it: the order strip has pages (an order on
another page is outlined on its page arrow), lists show fewer rows, windows cover more of the
main window. Scripts marked `# layouts: both` in their header comments work in both:
`--small` plays them at 800x600 as well (the command line's `--layout=800x600 --size=800x600`
overrides their options; their pictures go to `<script>@800x600`), and `--only-small` plays
only those runs. Script names may be patterns (`'tutorial-*'`). Every script is marked except
those written for one layout: `lesson-panel`, `lesson-uncover` and `lesson-pager` play at
800x600 only; `learn-resume` (no compact panel) and `lesson-long-step` (two minutes of play)
at 1024x768 only. A script that
plays at both should not depend on the layout: give an order that may be on another page with
`repeat 3 until { order = "..." }` around its click (once at 1024x768, the page arrow first at
800x600), and wait for a row of a list with `wait-for`.

Run them, with `--small`, after changing the client's windows, the tutorials or the input
lock. With the lesson checks (`tools/check_lessons.py`, and its `--audit` of what each step
lets through and shows, docs/LEARNING.md) they are the routine for the learning content.
After changing the session, the turn flow or the battle windows, also play
`battle-strategic` with the `asan` build's client, which turns a dangling pointer into a
failure (`ASAN_OPTIONS=detect_leaks=0 ./build/asan/opense4 --input-script=...`; the
graphics drivers leak a little at exit).

`OPENSE4_BATTLE_SOAK=N` with `OPENSE4_CLASSIC_DATA=auto` runs an opt-in soak test
(`-tc="battle flow: quick*"`): N quick turn-based games on your install from seed
`OPENSE4_BATTLE_SOAK_SEED` (default 1), the player's empire played by its ministers for
`OPENSE4_BATTLE_SOAK_TURNS` turns (150), every battle fought as the Strategic Combat window
fights it, and each turn compared with the same turn played without stops
(`_OPP` computer players, 4; `_SYSTEMS` systems, 12; `_SIM=1` for simultaneous turns). Under
the `asan` build it finds memory errors in the battle flow.

`tests/test_classic_save.cpp` checks the original's saved-game format (docs/spec/08): the
container's test vector, OpenSE4 games of both turn styles through export and import, and
damaged files and other data sets. Three opt-in variables add the original's own saves,
which are never fixtures (keep them out of the repository):

```sh
# Every .gam of a folder (and of the install's SaveGame folder): decoded to the end, the
# spec's invariants and counts, imported, played on without desync, exported again with only
# the differences the spec explains (OPENSE4_ORIGINAL_SAVES_TURNS turns each, default 2).
OPENSE4_CLASSIC_DATA=auto OPENSE4_ORIGINAL_SAVES=/path/to/saves ./build/debug/tests/opense4_tests -tc="classic save*"
# One game played on for 5 turns, saved and loaded in OpenSE4's format, exported again
# (OPENSE4_ORIGINAL_SAVE_EXPORT=FILE.gam keeps that export, to load it in the original).
OPENSE4_CLASSIC_DATA=auto OPENSE4_ORIGINAL_SAVE_PLAY=/path/to/GAME.gam ./build/debug/tests/opense4_tests -tc="classic save*"
```

The audio check plays the scripts of `tests/audio` with sound on, through SDL's `disk`
audio driver, which writes the mix to a file in real time instead of to a sound card, and
then reads that file. It fails on clicks (a jump between neighbouring samples far above the
sound around it: a sound started, cut off or broken off without a fade), on digital silence
that begins or ends away from zero, on three seconds or more without music, on clipping,
and on any track or sound the log says could not be played or music that ran dry. Where
the log says the game was muted in the background, the capture must fade into digital
silence, stay silent until the log says it was unmuted and have its music back right
after. It needs your install and about a minute of real time:

```sh
OPENSE4_CLASSIC_DATA=auto python3 tools/check_audio.py                  # every script, side by side
OPENSE4_CLASSIC_DATA=auto python3 tools/check_audio.py --keep /tmp/audio intro-loop   # keep the capture and log
```

`intro-loop` stays on the intro screen past the end of its 48-second track, with the device
at 32-bit float and 48 kHz as Windows usually mixes (a script's `# device: F32 2 48000`
header line); `game-turns` cuts effects off with others, ends turns and changes the
background track at turn 65, at the disk driver's 16-bit 44.1 kHz; `background-mute`
takes the focus from the game's window, minimizes and covers it (`window-event` steps)
and presses buttons while it is muted. Run it after changing `src/client/audio*`. The mixer's fades and ramps, the decoding and the music thread have
unit tests of their own (`tests/test_audio_mix.cpp`, on our own `tests/fixtures/audio`).

The Windows tests also run under Wine. `tools/package_release.sh` runs them in a Wine
prefix of its own set to Windows 7 SP1 (`build/_tools/wine-win7`); by hand:

```sh
cmake --preset dist-windows && cmake --build --preset dist-windows -j 4
export WINEPREFIX=$PWD/build/_tools/wine-win7 WINEDLLOVERRIDES="winemenubuilder.exe=d"
wine winecfg -v win7                                   # once: report Windows 7 SP1
wine build/dist-windows/tests/opense4_tests.exe
python3 tools/check_windows_imports.py build/dist-windows/*.exe
```

## Input scripts

`opense4 --input-script=FILE` plays a script of clicks, keys and checks against the running
client. Its events are made into SDL events at each frame boundary and handled exactly like
a player's mouse and keyboard: the mode's filter first (a tutorial's input lock), then Dear
ImGui. During a run the frame time is fixed (1/60 s), the seed is fixed (1 unless the script
or the command line gives one), the player's own mouse and keyboard are ignored (the desktop
pointer too, which Dear ImGui's SDL backend otherwise reads while the window has the focus)
and no sound plays (unless `--audio` asks for it, as the audio check below does), so a script
does the same thing every time. The Windows build plays the
same scripts (checked under Wine, where it opens a real window: a pointer step may then take a
frame more to aim, as the layout settles at that window's size). When a step fails, the client prints the
script line, why it failed and where the game was, saves a picture of that frame and exits
with 1; a script that ends exits with 0 and prints
`input-script FILE: passed (N steps, M frames)`. A script also fails at the step under way
when Dear ImGui reports one of its recoverable errors in a frame (a widget used the wrong
way, such as the cursor placed past a window's content with no item after it), with Dear
ImGui's message: such a window shows players nothing, but must not pass.

**Dear ImGui's errors.** Players never see Dear ImGui's red error tooltip or its assert:
every distinct error (window and message) is written once to `opense4.log`, and release
builds stop there (`client/ui/imgui_errors.hpp`); they show no tooltip for widgets that share
an id either. Debug builds keep the tooltip and the assert, so that a developer sees the error
at once; under an input script they do not assert, and the script fails instead.

```sh
SDL_VIDEO_DRIVER=offscreen ./build/debug/opense4 --input-script=tests/input/tutorial-first-steps.script \
    --script-output=/tmp/shots       # its screenshots and the failure picture (default: the temp folder)
./build/debug/opense4 --tutorial=first-steps --record-input=/tmp/first.script   # record a session
```

`--record-input=FILE` writes what you do as a script, to start one from: each click names
what is under the pointer, pauses become waits and the lesson's progress `wait-step` lines.
Recording a script run (both options) records the script's own events, which is how the
recorder is tested.

`tests/input` holds the scripts (each starts with comments that say what it plays):

| Scripts | What they play |
|---|---|
| `tutorial-*.script` | Each of the seven tutorials from its first step to its result, at 1024x768 and (with `--small`) 800x600, under the input lock: every step done by clicking what it tells the player to click; Next only on steps that explain, never Skip or Free Play (the runner checks); the wrong choices a step refuses tried with `refused` (Base in tutorial 4's vehicle types, another hull, design type, race, list row, tab or message type, the buttons of windows earlier steps left open, Esc and Enter once a step's window has closed or over a window it needs, right clicks on the system view and the galaxy panel, Send before the treaty is chosen). `tutorial-wrong-ship.script` tries to give tutorial 2's Explore order to the colony ship: the ship arrows are refused while the order is given, and Explore while the colony ship is selected; `tutorial-wrong-fleet.script` tries to add the colony ship to tutorial 5's fleet: its row is refused, and so is the fleet's list |
| `training-*.script` | Each training game's briefing pages (Previous, Next, Close Page), its first turns with their hints, Hide, the T button and Ctrl+H, Leave Game with its question |
| `lesson-results.script` | The result dialog, won and lost (its recap and the next game, Next Game, Keep Playing, Try Again, Learn), on two quick training games of our own in `tests/input/learn` |
| `lesson-lock-windows.script`, `lesson-lock-simulator.script` | The tutorial input lock with windows over each other: the designer over Designs, the Combat Simulator over Designs; what the window in front does not allow is refused (docs/LEARNING.md "The input lock") |
| `lesson-recovery.script`, `lesson-uncover.script` | The way back to a closed window (one and two windows deep) and past a window that covers the outline, with no Skip meanwhile (docs/LEARNING.md "Getting back") |
| `lesson-rewind.script`, `lesson-rewind-simulator.script` | A window a step works in closed with its work (tutorial 4's designer, tutorial 6's Combat Simulator before and after its tactical battle, closed with Free Play since the lock refuses it): the lesson goes back to the first step that set the work up, says why and how to open the window again, and the steps are played again |
| `lesson-keep-fleet.script`, `lesson-keep-queue.script`, `lesson-keep-strategies.script` | A window closed at a step that needs nothing it held (Fleet Transfer after the fleet was made, Research after the projects were chosen, the queue window after the ship was queued, the simulator at tutorial 6's Strategies): the lesson stays at that step, shows the way back (Construction Queues' list, not the order under it), and nothing is done twice |
| `lesson-keyboard.script`, `lesson-double-next.script` | Keyboard navigation under the lock: Ctrl+Tab refused even from a text field, Tab within the window in front, Space and Enter pressing no button the keyboard was left on while Space still does what the step allows; a double click on the panel's Next moving one step |
| `lesson-skip.script`, `lesson-long-step.script` | Skip only after ten seconds without a way back, and no cascade after it; no Skip on a step that waits on the turns; End Turn refused under a window, with the way back (close it) |
| `lesson-prompt-keys.script`, `lesson-result-keys.script` | Keys that answer a lesson's questions are not also main-window keys; the notes after refused clicks and keys; Free Play off at a lesson's start |
| `end-turn-question.script` | End Turn while a window is open does nothing, by click or key (every window is modal); with the window closed its question comes up and takes the input |
| `text-fit.script` | Text size 1.5 (`--text-size=1.5`): `assert-fits` in the main window, the command buttons' windows, Ministers, Empires, every page of the Race Report, the Treaty Grid, Scores, End Turn's question and the manual; Colony Type and the reading text grow with the setting |
| `empire-tables.script` | The Treaty Grid and Scores of a twelve-empire game, both pages: every name, code and figure fits and is whole (`assert-fits`, `assert-whole`; GitHub issue #3) |
| `race-report.script` | The Race Report from Empires' Our Race: its four tabs and Close; modal over Empires (Empires' Close and Our Race do nothing under it), Esc closing the report and not Empires |
| `modal-designer.script`, `modal-battle.script` | Every window is modal: under the ship designer and under a battle in Strategic Combat, End Turn (click, F12, Enter), the command buttons and their keys and the selectors do nothing; Esc goes to the window in front; once the window closes End Turn works |
| `front-learn.script` | The intro's Tutorial and Scenario buttons, the Learn window's tabs, starting a lesson and leaving it |
| `lesson-panel.script` | The lesson panel at 800x600 on a tutorial of our own in `tests/input/learn`: prompts and its Leave question over the panel dragged under them, placing itself again, the compact panel and More, its keys and Shift+F1 under the input lock |
| `lesson-pager.script` | The order strip's pages at 800x600 on a tutorial of our own in `tests/input/learn`: an outlined order on another page outlined on its page arrow, the panel's hint to press it (and the note after a refused click), the way back first while a window covers the arrow, the arrow on an explanation step (docs/LEARNING.md "Getting back") |
| `learn-resume.script` | The intro's hint by Tutorial, the Learn window's count, Next and Done marks and the lesson it chooses, and resuming a tutorial left in a window and in the main window, on tutorials of our own in `tests/input/learn` |
| `manual-front.script`, `manual-game.script` | The manual: contents tree, links to pages and sections, window and Help links, Back and Forward (buttons and Alt+arrows), search, Contents, Shift+F1 |
| `list-windows.script` | Planets, Colonies, Ships and Construction Queues: sort headings, tabs and filters, the arrow column and the wheel; item and Design Report pop-ups from right-clicks |
| `combat-windows.script` | Tactical Combat on a sample battle: zoom and pan, the Combat Piece Report, Combat Options, the Orders menu and Resolve Combat |
| `ground-combat.script` | Ground Combat on a sample strategic battle (`--open=ground-combat`): Begin, the rounds, Close, then the rest of the battle |
| `log-details.script` | The Log's details pane on an entry without a body (a ship scrapped at the home yard; GitHub issue #9): no Dear ImGui error |
| `combat-replay.script` | Combat Replay on a battle of the last turn: the overview, Combat Replay Options' check boxes, Next to the last combat turn, Stop Replay |
| `battle-strategic.script` | A battle that stops the player's End Turn: an enemy ship comes through a warp point into the player's sector in a computer player's turn; the notice, Strategic Combat's question, Strategic, End Turn doing nothing over the battle's window, Close; the turn then ends exactly once, and the next End Turn ends the next one. Play it with the `asan` client too (v0.8.1 crashed here: see "Crash reports") |
| `system-report.script` | The system report of an empty sector: the system's picture inside the panel's frame rail (GitHub issue #2), its texts fitting their places |
| `report-up-arrow.script` | The report panel's up-arrow back to a sector's list: shown only for a report opened from the list |
| `fleet-row.script` | On tutorial 5's fleet: the fleet as one row of the sector's list (Shift and a click tag it), the Fleet Report alone, a member made the leader with a click and its Ship Report opened with a right-click, the up-arrow; the fleet and a ship tagged together given one order |
| `selection-other-system.script` | Another system shown keeps the selection: the report and its tab across a galaxy-panel click and the Galaxy Map's Goto System, a right-click in the system panel doing nothing, a facility's report from Facil, Detail again when a report is filled afresh |
| `designs-options.script` | The Designs window's Hide Obsolete and Stats\Strategy are the empire's options: closed and opened again, the window is as it was left; an enemy tab switches Stats\Strategy off (GitHub issue #1) |
| `only-latest.script` | The Only Latest boxes of Set Construction Queue and Create Design write the Empire Options rows |
| `colonize-pick.script`, `follow-warp.script` | On a training game of our own in `tests/input/learn-orders` (two colony ships at home): Colonize's Pick Object window and Cancel, a wrong pick failing on arrival with the "Colonize" message box, the moons settled, population moved between them with Cargo Transfer; a warp the view follows to the arrival system |
| `sliders.script` | Dragging sliders: a combat strategy's settings and OpenSE4's Settings |
| `game-setup.script` | The setup screens: Load Game with Change Directory, Quick Start's picker, Game Setup's pages, Add New with a name from the list picker and an e-mail address, Begin Game, and Change Email in Empire Status |
| `mods-window.script` | The Mods window on the fixture mods of `tests/fixtures/mods` (`--mods-dir`): each mod's details, enabling by button and double click, a requirement missing then met, the order and Move Up, Done refused with the reason when a patch does not fit the installed data, then Done reading the data again; Cancel keeping the choice (docs/sdk/packages-and-data.md "Choosing mods in the game") |
| `mods-setup.script` | Game Setup's and Quick Start's line about the mods and their Mods button, the setup kept across Cancel, a quick game with the mods whose hull the designer offers |
| `mods-saved-game.script` | Saved games played with other mods: the Game Menu's Load of one ending the game and the front end's Other Mods window loading it without the mod; Load Game of a modded game from the title screen, Load with Its Mods reading the data again with the game's mod |
| `designer-picture.script`, `mod-hull-designer.script` | A design's own picture from the fixture `picture-pack` (the designer's choice, Designs, an edit, Save for SE IV refusing the game); a mod's new hull with its pictures in the designer |
| `computer-players-setup.script` | Computer players of the fixture mod `ai-fixture` in the setup screens (docs/SETUP.md "Computer players"): Game Setup's Computer Players window and line, Empire Setup's Computer Player row for a listed empire (the classic AI), Game Settings' "Computer players see everything" and Computer Player Limits; in the game begun so, Captain's notes in the AI notes view (`Ctrl+Shift+N`): the list, the galaxy panel, a noted sector (`sector:noted`, `system:noted`) and a noted colony's report |
| `computer-player-errors.script` | Quick Start's Computer Players with the fixture's Faulty, which fails when asked for its orders: the main window's notice after End Turn, Details and the Computer Player Errors window with the traceback, the next turn's notice and Dismiss |
| `computer-player-battle.script` | A battle that stops End Turn (seed 23, as `battle-strategic.script`) with the fixture's Captain on the computer's side, fought in Strategic Combat with Captain giving its side's orders each combat turn; the turn going on with those answers and nothing failing; Captain's battle notes in the AI notes view |
| `computer-players-host.script`, `computer-players-lobby.script` | Network games with computer players of a mod: the host form's choice and "Computer players see everything", the lobby's slot players changed by the host, Add Computer; and the lobby as a joining player sees it, against a dedicated host the runner starts (`# server:`, below) |

Scripts marked `# ci: fixture-data` need nothing but our own content and also run on a game
folder made from `tests/fixtures` (CI). Scripts marked `# layouts: both` also play at 800x600
(`--small`, see "Tests"). A script with a `# server: ARGS` line among its first comments plays
against a dedicated host: `tools/run_input_tests.py` starts `opense4-server` from the client's
folder with ARGS (paths from the repository's root) on a free port of 127.0.0.1, and the
client joins its lobby (`--open=multiplayer:join=...`); the server stops when the script ends.

### The format

One step per line; `#` starts a comment, and `"..."` quotes a word with spaces. Before the
first step, `options ...` lines give the client's command-line options (the command line can
override them) and `timeout N` sets how many frames later waits may take (default 1800, 30 s
of play).

Targets name what a pointer step points at:

| Target | Meaning |
|---|---|
| `tag:<name>` | a UI tag (docs/LEARNING.md "UI tags"): `tag:research:queue`, `tag:lesson:next` |
| `window:<id>` | a classic window (its `window:<id>` tag) |
| `item:<label>` | a widget by its label: `item:"Keep Playing"`, `item:##down`, `item:"*(suggested)"` (`*` and `?` are patterns over the label as shown). Windows and pop-ups are `item:window:<name>`. Our own widgets name themselves: classic buttons, list headings, report tabs, lamp rows, lesson and design rows, manual links (`item:link:economy#trade`), the manual page and its sections in view (`item:page:research`, `item:anchor:the-research-queue`), the main window's report rows by kind (`report:colony`, `report:planet`, `report:ship`, `report:fleet` for one of the player's fleets, `report:other`, and `report:fleet-member` in the Fleet Report) and the list's tag count (`tagged:2`), the tactical map's pieces (`piece:own`, `piece:enemy`, `piece:other`) and the squares around the selected one (`square:1,-2`); a lesson's way back (`recovery:<tag>`, the part outlined, and `hint:<text>`, its line in the panel) and the note after a refused click or key (`note:<text>`, its lines joined by spaces) |
| `sector:<x>,<y>`, `sector:<query>` | a sector of the system view; a query is words joined by `+` and negated by `!`: `empty`, `home`, `colony`, `planet`, `colonizable`, `star`, `warp-point`, `ship`, `enemy`, `selected`, `noted` (something in it has a computer player's note, while the AI notes view is on), `any` (`sector:planet+!colony`) |
| `system:<n>`, `system:<query>` | a system of the galaxy panel by number, or `home`, `shown`, `explored`, `noted` (as for sectors), `any` |
| `at:<x>,<y>` | a point of the classic frame, in frame pixels |

After a target, `@x,y` picks a point in its rectangle in frame pixels from the left and top
(negative: from the right and bottom) or as `x%,y%`; `in=<scope>` keeps to widgets drawn by a
window (its id, `main`, `lesson`, `front`), in a Dear ImGui window of that name, or inside a
UI tag (`item:next in=tag:cycle:ship`, the ship selector's right arrow); `nth=N` takes the
N-th match. Prefer tags and labels to places: other windows' layouts move, their tags and
labels stay. A pointer step waits for its target (and for a widget to be enabled),
aims again if it moved just before the press, and keeps separate clicks at one place apart
from a double click.

| Step | What it does |
|---|---|
| `click T`, `double-click T`, `right-click T`, `middle-click T` | press and release there; `shift`, `ctrl`, `alt` hold that key meanwhile; `refused`: the tutorial input lock must refuse the press; `optional`: skipped when T does not come within 10 frames |
| `drag T to T2` | press at T, move to T2 in `frames=N` steps (default 8), let go; `middle` or `right` for that button |
| `move T`, `wheel T N` | point there; turn the wheel N notches (one a frame, positive away from you) |
| `key CHORD`, `type "TEXT"` | a key chord as lessons write them (`F12`, `Ctrl+H`, `Shift+F1`, `Alt+LeftArrow`; `refused` as above); text into the field that has the keyboard |
| `window-event E` | the window event the system sends when the player leaves the game's window or comes back: `focus-lost`, `focus-gained`, `minimized`, `restored`, `hidden`, `shown`, `occluded` (covered) or `exposed`. A script run ignores the system's own (the game mutes itself in the background only when the script says so) |
| `wait N` | N frames |
| `wait-for T`, `wait-gone T` | until T is on screen, or no longer |
| `wait-window ID`, `wait-closed ID` | until that window is open, or closed |
| `wait-step N`, `wait-result R`, `wait-lesson SLUG`, `wait-screen S` | until the lesson's active step is N; its result is `none`, `done`, `won` or `lost`; that lesson (or `none`) runs; the screen is `game` or `front` |
| `wait-until { condition }`, `wait-turn N` | until a lesson condition holds ("since" counters from the start of the wait); until the game reaches turn N |
| `assert-present T`, `assert-absent T`, `assert-enabled T`, `assert-disabled T`, `assert-inside T T2` | T is on screen, or not; enabled or dim; T's point lies in T2's rectangle |
| `assert-fits SCOPE` | in that scope (a window id, `main`, `lesson`, `front`, or a Dear ImGui window's name): every text drawn into a box of its own (a button's caption, a text kept to its place) fits the box, and no labelled widget is cut off by its window by more than 4 frame pixels (a window that scrolls and a table's cells excepted) |
| `assert-whole SCOPE` | in that scope, every text drawn into a box of its own is drawn whole: none cut short with "…" to fit, none running out of its box |
| `assert-window ID`, `assert-no-window ID`, `assert-step N`, `assert-result R`, `assert-lesson SLUG`, `assert-screen S`, `assert-turn N` | as the waits, at once |
| `assert { condition }`, `assert-log "TEXT"`, `assert-no-log "TEXT"` | a lesson condition ("since" counters from the start of the game); some entry of the player's log has that text (letter case ignored), or none |
| `repeat N [until { condition }] ... end` | the steps between up to N times; with `until`, leaves as soon as the condition holds (checked before each pass, counters from the first) and fails if it never did |
| `screenshot FILE`, `echo TEXT` | save this frame's picture (relative to `--script-output`); print a line |
| `dump [SCOPE]`, `print KEY ...` | for writing scripts: print the UI tags, the widgets with their places, and the input lock; print condition counters (`print turn colonies`) |
| `audit` | print the lesson audit of the tutorial step the lesson is at, in the state the script brought the game to (docs/LEARNING.md "Checking the lessons: the audit") |

Every wait (and a pointer step's wait for its target) fails after its timeout (`timeout=N` on
the line, else the script's). Conditions are those of the lessons (docs/LEARNING.md
"Conditions").

## Crash reports

When the program dies of a fault or of an exception nothing caught, `src/client/crash_report.cpp`
appends a report to `opense4.log` in the user data folder and a message box says where it is
(not in automated runs: scripts, screenshots, `--lesson-check`, `SDL_VIDEO_DRIVER=offscreen`
or `dummy`). The next start renames that log to `opense4.previous.log`. A report looks like:

```
==== OpenSE4 crash report ====
Version: 0.8.1 (Windows, x86_64, Clang 21.1.0)
What: access violation (0xc0000005), reading 0x18
Stack:
  opense4.exe+0x4f1a2c
  ...
Last log lines:
  [  812.240 info ] A battle at system 4 (12, 10) stops the turn to be shown (1 human side(s))
==== end of the crash report ====
```

Windows reports come from an unhandled-exception filter (with a stack guarantee and a
thread of its own for stack overflows), Linux and macOS ones from signal handlers that
write with async-signal-safe calls only; there the message box is shown by the program
started again as `opense4 --crash-message=FILE`. An uncaught exception names its message.
Builds with AddressSanitizer keep the sanitizer's own handlers. The stack lines are
module and offset: turn them into functions and lines with the same build's binary,
`addr2line -f -C -e opense4 0x4c9d04` (Linux, from the `(+0x...)` of a line) or
`llvm-addr2line -f -C -e opense4.exe 0x1404f1a2c` (Windows: the offset plus the image base,
0x140000000 for our 64-bit builds). Release packages are stripped, so keep the unstripped
binary of each release (or rebuild the tag) to read them.

`OPENSE4_CRASH_TEST=fault` (or `exception`) makes the game crash on purpose right after
the handler is installed, to check the report on a platform, under Wine too:

```sh
OPENSE4_CRASH_TEST=fault SDL_VIDEO_DRIVER=offscreen ./build/debug/opense4; tail -20 ~/.local/share/OpenSE4/opense4.log
```

`tests/test_crash_report.cpp` checks the report of a segmentation fault and of an uncaught
exception in a child process (Linux and macOS).

## Continuous integration

GitHub Actions builds and tests every push and pull request
(`.github/workflows/ci.yml`). No job needs the original game: the tests that read an
installed copy stay off, the installed programs are only started with `--help`, and the
input scripts CI plays run on a game folder made from our own test fixtures.

| Job | What it checks |
|---|---|
| Linux / GCC debug, Clang debug | The `debug` preset on Ubuntu 26.04 with the distribution's SDL3, warnings as errors; the unit tests, the golden determinism checksums among them (every job checks them) |
| Linux / GCC ASan+UBSan | The unit tests built with the `asan` preset, warnings as errors; they stop at the first memory error, leak or undefined behaviour |
| Linux arm64 / GCC release | The `release` preset natively on GitHub's arm64 runner (`ubuntu-26.04-arm`) with the distribution's SDL3, warnings as errors; the unit tests. Optimized, so that the compiler could fuse floating-point operations if `-ffp-contract=off` did not forbid it |
| Linux armhf / GCC release (QEMU) | The `release` preset cross-compiled with the armhf toolchain file and Ubuntu's `arm-linux-gnueabihf` GCC (SDL3 without windows), warnings as errors; the unit tests under `qemu-arm`. The arm64 runners cannot all run 32-bit ARM code, so it runs on x86_64 |
| macOS / Apple Clang debug | The `debug` preset on Apple silicon (`macos-latest`) with Apple Clang, libc++ and Homebrew's SDL3, warnings as errors; the unit tests |
| Windows / MSVC release (VS 2022), (VS 2026) | The `release` preset with Visual Studio 2022 (on `windows-2022`) and Visual Studio 2026 (on `windows-2025`) and the Vulkan SDK's `glslc`, warnings as errors (`/W4 /WX`); the unit tests |
| Windows / llvm-mingw package (Windows 7 to 11) | `tools/package_release.sh windows` on Ubuntu, as for a release (`dist-windows`, warnings as errors): the cross build with llvm-mingw, the Windows 7 import check of every program, the unit tests under Wine (Ubuntu's Wine, set to Windows 7 SP1), the zip file and the installer (Ubuntu's NSIS), kept as the run's `opense4-windows` artifact |
| Windows / installer | Installs that installer silently (`/S`) on Windows Server 2025, checks the files, shortcuts and Apps & features entry, starts the installed programs, has the data checker read our fixture data set, and uninstalls silently, checking that nothing is left |
| Linux / input scripts (fixture data) | Builds the client and the dedicated server (`debug`, GCC) and plays the input scripts marked `# ci: fixture-data` (the manual, the Learn window, a training game's results, sliders, the setup screens, network lobbies with computer players of a mod) with `tools/run_input_tests.py --fixture-data --small` (those marked for both layouts also at 800x600): headless (SDL's offscreen driver) on Mesa's software OpenGL (llvmpipe), on the minimal data set and pictures of `tests/fixtures` with our built-in learning content. A failed run keeps its pictures as the `input-scripts-failure` artifact |

The Linux and macOS jobs run the tests in one process per core
(`.github/scripts/run_tests_parallel.sh`). The jobs keep the compiler's output
(ccache) and the sources FetchContent downloads
(`.github/scripts/fetchcontent_cache.cmake`) in the Actions cache. A change to
`cmake/Dependencies.cmake` starts from fresh downloads. The packages are always
built from fresh downloads of the sources; the Windows package job keeps llvm-mingw
in the Actions cache, keyed by its toolchain file. The workflows only read the repository, and pin every
action to a full commit SHA (the comment beside it names the release).

`.github/workflows/release.yml` runs for a version tag (`v*`), or by hand from the
Actions tab. It builds the Linux packages on Ubuntu (x86_64 and aarch64 natively, armhf
cross-compiled with its tests under QEMU) and the Windows packages (cross-built with
llvm-mingw) on Ubuntu with `tools/package_release.sh`, writes the checksums, and keeps
everything as one artifact, `OpenSE4-<version>`, and each package as its own
(`package-linux-aarch64` and so on, which `tools/fetch_arm_packages.sh` downloads). It
does not publish a release.

The MSVC jobs build with Microsoft's runtime DLLs, which Windows 7 has only after the
Visual C++ Redistributable and KB2999226 are installed, so CI does not hold them to
Windows 7. Their programs are not shipped.

The other input scripts (the tutorials, the training games, the list and battle windows)
stay local: they play the original game's data, art and fonts (its race presets, designs,
technologies and quadrants decide what is on screen), which CI cannot have. Run them with
`OPENSE4_CLASSIC_DATA=auto python3 tools/run_input_tests.py` before pushing a change to the
client's windows or the learning content.

`tools/cleanroom_check.py` is not part of CI, because it needs the installed game.
Run it yourself before committing documentation or content.

## Saved games of the original

`opense4-convert` converts saved games between the original's format (version 1.95) and
OpenSE4's, both ways, and describes the original's. The client does the same through Load
Game and Save Game's *Save for SE IV* (docs/SETUP.md, "Games of the original"); the format
and what it holds are in docs/spec/08-saved-games.md. It needs the data set the game was
played with: the installed game found automatically, or `--classic-dir`.

```sh
./build/debug/opense4-convert --info GAME.gam                        # versions, date, empires, counts per section
./build/debug/opense4-convert GAME.gam out.gam --to=opense4          # import: an OpenSE4 save
./build/debug/opense4-convert mine.gam ForSE4.gam --to=original      # export: a save for the original
./build/debug/opense4-convert GAME.gam again.gam --to=original       # an original save through OpenSE4 and back
./build/debug/opense4-convert --compare GAME.gam again.gam           # every field that differs, by its path
```

`--seed=N` fixes the keys of a written file (by default they are random, as the original's
are), and `-v` lists every detail of what was approximated. Exit status: 0 done, 1 a file
could not be read or written (the message names the section, record and byte), 2 no data
set or bad arguments, 3 `--compare` found differences. The library behind it is
`src/game/classic_save.hpp`: the container and its keys, a typed model of every section,
and the import into and export from a `GameState`.

## Headless runs

The client runs without a display through SDL's offscreen driver. This is useful for
screenshots in CI. It needs an installed copy of the game, as every run does;
`--classic-dir` points at one that auto-detection does not find:

```sh
SDL_VIDEO_DRIVER=offscreen ./build/debug/opense4 --quick-start=Terran --seed=7 --turns=20 --screenshot=/tmp/classic.png
SDL_VIDEO_DRIVER=offscreen ./build/debug/opense4 --quick-start=Terran --turn-style=simultaneous --seed=7 --turns=20 --screenshot=/tmp/simultaneous.png
SDL_VIDEO_DRIVER=offscreen ./build/debug/opense4 --load=/path/to/GAME.gam --turns=5 --screenshot=/tmp/loaded.png   # a saved game, OpenSE4's or the original's
```

Without an installed copy the client logs why and exits with status 1.

Screenshot runs use a fixed frame time and keep the pointer off the window, so the same
command gives the same picture on every machine. `--select=moving` (or `fleet`, or a
vehicle id) selects one of your vehicles after `--turns`, for example to show its
movement line. The Windows build renders headless under Wine too, with Vulkan:

```sh
SDL_VIDEO_DRIVER=offscreen WINEDLLOVERRIDES="winemenubuilder.exe=d" wine build/dist-windows/opense4.exe \
    --classic-dir="Z:/path/to/Space Empires IV Deluxe/se4" --quick-start=Terran --seed=7 --screenshot=Z:/tmp/win.png
```

`tools/render_scenes.sh` renders a set of scenes with a build and renderer and compares
two such sets pixel by pixel (docs/ENGINE.md, "Same on every platform").
