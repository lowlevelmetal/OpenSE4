# The part the Linux cross toolchains share (aarch64-linux-gnu.cmake and
# arm-linux-gnueabihf.cmake set OPENSE4_TRIPLE and OPENSE4_QEMU_ARCH, then
# include this). See docs/BUILDING.md, "ARM Linux".
#
# The compiler is <triple>-gcc and <triple>-g++, unless CMAKE_C_COMPILER and
# CMAKE_CXX_COMPILER are given (Clang: also CMAKE_<LANG>_COMPILER_TARGET and
# CMAKE_SYSROOT). The target's headers and libraries come from
#   - a sysroot, when CMAKE_SYSROOT is given;
#   - else Debian's and Ubuntu's multiarch folders (/usr/lib/<triple>), where
#     the target's packages go (apt install libfoo-dev:arm64 or :armhf);
#   - else a cross compiler's own folder, /usr/<triple> (Arch, Fedora).
# pkg-config sees only the target's .pc files. Host programs (glslc) come from
# the build machine. With QEMU's user mode installed, the tests run through it.

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_LIBRARY_ARCHITECTURE ${OPENSE4_TRIPLE})

if(NOT CMAKE_C_COMPILER)
    set(CMAKE_C_COMPILER ${OPENSE4_TRIPLE}-gcc)
endif()
if(NOT CMAKE_CXX_COMPILER)
    set(CMAKE_CXX_COMPILER ${OPENSE4_TRIPLE}-g++)
endif()

set(opense4_target_root "")
if(CMAKE_SYSROOT)
    set(opense4_target_root "${CMAKE_SYSROOT}")
elseif(NOT IS_DIRECTORY "/usr/lib/${OPENSE4_TRIPLE}" AND IS_DIRECTORY "/usr/${OPENSE4_TRIPLE}")
    set(opense4_target_root "/usr/${OPENSE4_TRIPLE}")
    list(APPEND CMAKE_FIND_ROOT_PATH "${opense4_target_root}")
endif()

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
if(opense4_target_root)
    set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
    set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
    set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
    set(ENV{PKG_CONFIG_SYSROOT_DIR} "${opense4_target_root}")
    set(ENV{PKG_CONFIG_LIBDIR} "${opense4_target_root}/usr/lib/${OPENSE4_TRIPLE}/pkgconfig:${opense4_target_root}/usr/lib/pkgconfig:${opense4_target_root}/usr/share/pkgconfig:${opense4_target_root}/lib/pkgconfig")
else()
    # Multiarch: the host's own libraries are in /usr/lib/<host triple>, which
    # CMake does not search for this target.
    set(ENV{PKG_CONFIG_LIBDIR} "/usr/lib/${OPENSE4_TRIPLE}/pkgconfig:/usr/share/pkgconfig")
endif()

# ctest (and anything else that runs a built program) goes through QEMU. Its -L
# names where the target's dynamic loader and libraries are; QEMU falls back to
# the real root for anything not there.
find_program(OPENSE4_QEMU NAMES qemu-${OPENSE4_QEMU_ARCH} qemu-${OPENSE4_QEMU_ARCH}-static)
if(OPENSE4_QEMU)
    if(opense4_target_root)
        set(CMAKE_CROSSCOMPILING_EMULATOR "${OPENSE4_QEMU};-L;${opense4_target_root}")
    elseif(IS_DIRECTORY "/usr/${OPENSE4_TRIPLE}")
        set(CMAKE_CROSSCOMPILING_EMULATOR "${OPENSE4_QEMU};-L;/usr/${OPENSE4_TRIPLE}")
    else()
        set(CMAKE_CROSSCOMPILING_EMULATOR "${OPENSE4_QEMU}")
    endif()
endif()
