# Building OpenSE4

OpenSE4 is plain CMake. It builds with GCC, Clang and MSVC on Linux, Windows and macOS.
Linux is the primary development platform. Windows builds are routine. macOS builds
are best effort and receive less testing.

What gets built:

| Target | What it is |
|---|---|
| `opense4` | The game client: Vulkan 1.3 with an OpenGL 3.3 fallback |
| `opense4-server` | Dedicated multiplayer host, and the PBEM turn processor |
| `opense4-datacheck` | Loads and validates an installed or modded classic data set |
| `opense4-observe` | Linux-only harness for observing the original game (see docs/CLEANROOM.md) |
| `opense4_tests` | Unit tests. They use only our own fixtures |

## Requirements

- A C++23 compiler: GCC 14+, Clang 18+ or Visual Studio 2022 17.10+.
- CMake 3.25+ and Ninja. Visual Studio's own generator also works on Windows.
- SDL3 3.2+. A system copy is used if found; otherwise CMake fetches and builds it.
- `glslc` to compile the shaders at build time. It comes with the Vulkan SDK or the
  `shaderc` package.
- Vulkan headers. A system copy is used if found; otherwise they are fetched.

CMake fetches the remaining dependencies at pinned versions with verified hashes:

- Dear ImGui, volk, VMA, toml++, stb, dr_mp3 (music) and doctest;
- miniupnpc, for automatic router port forwarding.

The first configure therefore needs network access. Later builds do not.

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

1. Install Visual Studio 2022 with **Desktop development with C++**. It includes CMake
   and Ninja.
2. Install the [Vulkan SDK](https://vulkan.lunarg.com/sdk/home#windows), which
   provides `glslc` and the headers. The installer sets `VULKAN_SDK`, and the build
   looks for `glslc` there.
3. Open **x64 Native Tools Command Prompt for VS 2022** and run:

```bat
cmake --preset release
cmake --build --preset release
build\release\opense4.exe
```

SDL3 is fetched automatically unless CMake can find an installed copy. Pass
`-DCMAKE_PREFIX_PATH=C:\path\to\SDL3` to use a prebuilt SDL3. The SDL3 DLL is copied
next to the executable.

You can also open the source folder directly in Visual Studio ("Open Folder"), which
reads `CMakePresets.json`.

Every Windows program carries an application manifest that sets the UTF-8 code page
(`packaging/windows/opense4.manifest`, honoured from Windows 10 1903 on), so file names
with letters outside ASCII work as on Linux. `.gitattributes` turns off line-end
conversion, so a clone with `core.autocrlf` builds the same program as on Linux.

## macOS

```sh
brew install cmake ninja sdl3 shaderc vulkan-headers molten-vk
cmake --preset release
cmake --build --preset release
./build/release/opense4 --renderer=opengl
```

Vulkan runs on macOS through MoltenVK. If the Vulkan renderer fails to initialize,
use `--renderer=opengl`.

## Presets and options

| Preset | Build type | Notes |
|---|---|---|
| `debug` | Debug | The default for development |
| `release` | RelWithDebInfo | Optimized, with symbols |
| `asan` | Debug | AddressSanitizer and UndefinedBehaviorSanitizer (GCC and Clang) |
| `dist-linux` | Release | Redistributable Linux build (see "Release packages") |
| `dist-windows` | Release | Redistributable Windows build, cross-compiled with MinGW-w64 |
| `dist-mingw` | Release | The same Windows build, made natively in MSYS2 (MinGW-w64) |

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
platforms from one Linux machine. In MSYS2 on Windows (the UCRT64 shell) it builds
the Windows packages natively instead, with the `dist-mingw` preset:

- `dist/OpenSE4-<version>-linux-x86_64.tar.gz`
- `dist/OpenSE4-<version>-windows-x86_64.zip`
- `dist/OpenSE4-<version>-windows-x86_64-setup.exe`, the same programs as an installer
- `dist/OpenSE4-<version>-SHA256SUMS.txt`

Each package holds `opense4`, `opense4-server` and `opense4-datacheck`, stripped,
with the README, `LICENSE` (GPL 3.0 or later) and `THIRD_PARTY_NOTICES.txt`. Our own fonts (Noto Sans, SIL Open
Font License) are built into the game, so nothing else needs to sit next to it.
The Linux package also holds the desktop entry, icons and AppStream metadata under
`share/`, and `install-desktop-entry.sh`, which adds the game to the user's
application list (see "Installing on Linux"). The script refuses to package a tagged
version that has no `<release>` entry in the AppStream metadata.
Nothing from the original game is included: players point OpenSE4 at their own
installed copy, without which the game does not start.

- **Linux** (`dist-linux`): SDL3, the C++ runtime and every other library are linked
  statically. Only the C library stays shared, because SDL loads the system's
  X11/Wayland, audio and GPU driver libraries at run time. The binaries run on
  glibc 2.34 and later (Ubuntu 22.04, Debian 12, Fedora 35, RHEL 9, SteamOS 3 and
  newer), even when built on a newer distribution: `cmake/GlibcCompat.cmake` routes
  the few newer glibc functions to older versions or small built-in
  implementations. `tools/check_glibc.sh` reports what a binary needs.
- **Windows** (`dist-windows`, or `dist-mingw` in MSYS2): built with MinGW-w64
  (`mingw-w64-gcc` for the cross build) and linked with `-static`. The executables need only Windows' own DLLs and the
  Universal C Runtime, which ships with Windows 10 and 11. The game is a windowed
  application; started from a console it still prints `--help` and its log there.
  With Wine installed, the script runs the Windows tests through it.

Requirements beyond a normal build:

- network access the first time (SDL3 is fetched and built as a static library);
- `mingw-w64-gcc` for Windows;
- `bsdtar` for the zip file;
- NSIS or Wine for the installer (see "The Windows installer").

In MSYS2, install `git` and the UCRT64 packages `gcc`, `cmake`, `ninja`, `shaderc`,
`libarchive` (for `bsdtar`) and `nsis`, each named `mingw-w64-ucrt-x86_64-<name>`.

Pass `--skip-tests` to package without running the tests.

### The Windows installer

`packaging/windows/opense4.nsi` is an NSIS 3 script for a standard installer
(welcome, licence, components, folder, finish). It installs for all users into
Program Files and adds:

- a Start menu entry and, if chosen, a desktop shortcut;
- an Apps & features entry with the icon, version and an uninstaller.

An update goes into the folder of the previous install. The uninstaller leaves
saved games and settings in `%APPDATA%\OpenSE4` alone. Silent use works as with any
NSIS installer: `setup.exe /S`, and `/D=C:\path` (last, unquoted) for another folder.

The script uses `makensis` when it is on the PATH (Debian and Ubuntu package it as
`nsis`, MSYS2 as `mingw-w64-ucrt-x86_64-nsis`). Otherwise it runs the official Windows build of NSIS under Wine. That build
is downloaded once into `build/_tools` and checked against a pinned SHA-256, or
taken from `NSIS_DIR`.

`opense4.exe` carries the icon and version information from
`packaging/windows/opense4.rc.in`. The icon (`opense4.ico`) and the installer's side
picture are rendered from the SVG icon by `tools/render_icons.sh`.

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

## Offline builds

Point FetchContent at already-downloaded sources:

```sh
cmake --preset debug -DFETCHCONTENT_FULLY_DISCONNECTED=ON \
    -DFETCHCONTENT_SOURCE_DIR_IMGUI=/path/to/imgui ...
```

The dependency names are `IMGUI`, `VOLK`, `VMA`, `VULKANHEADERS`, `TOMLPLUSPLUS`,
`STB`, `DRLIBS`, `DOCTEST` and `MINIUPNPC`. The sources for each dependency are also under
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

`test_determinism.cpp` compares fixed games and battles with golden checksums, so every
compiler must compute exactly the same game (docs/ENGINE.md, "Same on every
platform"). If a deliberate rules change moves them, print the new values with
`OPENSE4_PRINT_GOLDEN=1 ./build/debug/tests/opense4_tests -tc="determinism*" -s` and
paste them in. A failure on one compiler only means the engine depends on something
that compiler does differently; the message names the first turn and part of the state
that differ.

The Windows tests also run under Wine:

```sh
cmake --preset dist-windows && cmake --build --preset dist-windows
WINEDLLOVERRIDES="winemenubuilder.exe=d" wine build/dist-windows/tests/opense4_tests.exe
```

## Continuous integration

GitHub Actions builds and tests every push and pull request
(`.github/workflows/ci.yml`). No job needs the original game: the tests that read an
installed copy stay off, and the installed programs are only started with `--help`.

| Job | What it checks |
|---|---|
| Linux / GCC debug, Clang debug | The `debug` preset on Ubuntu 26.04 with the distribution's SDL3, warnings as errors; the unit tests, the golden determinism checksums among them (every job checks them) |
| Linux / GCC ASan+UBSan | The unit tests built with the `asan` preset, warnings as errors; they stop at the first memory error, leak or undefined behaviour |
| Windows / MSVC release | The `release` preset with Visual Studio 2022 and the Vulkan SDK's `glslc`, warnings as errors (`/W4 /WX`); the unit tests |
| Windows / MinGW-w64 package | `tools/package_release.sh windows` in MSYS2 (`dist-mingw`, warnings as errors): the release build, the unit tests, the zip file and the installer, kept as the run's `opense4-windows` artifact |
| Windows / installer | Installs that installer silently (`/S`), checks the files, shortcuts and Apps & features entry, starts the installed programs, and uninstalls silently, checking that nothing is left |

The Linux jobs run the tests in four processes at once
(`.github/scripts/run_tests_parallel.sh`). The jobs keep the compiler's output
(ccache) and the sources FetchContent downloads
(`.github/scripts/fetchcontent_cache.cmake`) in the Actions cache. A change to
`cmake/Dependencies.cmake` starts from fresh downloads. The packages are always
built from fresh downloads. The workflows only read the repository, and pin every
action to a full commit SHA (the comment beside it names the release).

`.github/workflows/release.yml` runs for a version tag (`v*`), or by hand from the
Actions tab. It builds the Linux package on Ubuntu and the Windows packages in
MSYS2 with `tools/package_release.sh`, writes the checksums, and keeps everything as
one artifact, `OpenSE4-<version>`. It does not publish a release.

`tools/cleanroom_check.py` is not part of CI, because it needs the installed game.
Run it yourself before committing documentation or content.

## Headless runs

The client runs without a display through SDL's offscreen driver. This is useful for
screenshots in CI. It needs an installed copy of the game, as every run does;
`--classic-dir` points at one that auto-detection does not find:

```sh
SDL_VIDEO_DRIVER=offscreen ./build/debug/opense4 --quick-start=Terran --seed=7 --turns=20 --screenshot=/tmp/classic.png
SDL_VIDEO_DRIVER=offscreen ./build/debug/opense4 --quick-start=Terran --turn-style=simultaneous --seed=7 --turns=20 --screenshot=/tmp/simultaneous.png
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
