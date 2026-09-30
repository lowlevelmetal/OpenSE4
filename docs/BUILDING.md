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

- Dear ImGui, volk, VMA, toml++, stb and doctest;
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

| CMake option | Default | Effect |
|---|---|---|
| `OPENSE4_BUILD_TESTS` | ON | Build `opense4_tests` and fetch doctest |
| `OPENSE4_WARNINGS_AS_ERRORS` | OFF | Treat warnings as errors (CI uses this) |
| `OPENSE4_ENABLE_UPNP` | ON | Build with miniupnpc. OFF compiles a no-op port mapper |

The code must compile without warnings under
`-Wall -Wextra -Wpedantic -Wshadow -Wconversion` (`/W4` on MSVC).

## Offline builds

Point FetchContent at already-downloaded sources:

```sh
cmake --preset debug -DFETCHCONTENT_FULLY_DISCONNECTED=ON \
    -DFETCHCONTENT_SOURCE_DIR_IMGUI=/path/to/imgui ...
```

The sources for each dependency are also under `build/<preset>/_deps/<name>-src` after
any online configure. You can reuse them for other build directories or worktrees
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
copy anything into the repository.

## Headless runs

The client runs without a display through SDL's offscreen driver. This is useful for
screenshots in CI:

```sh
SDL_VIDEO_DRIVER=offscreen ./build/debug/opense4 --classic --seed=7 --screenshot=/tmp/classic.png
```
