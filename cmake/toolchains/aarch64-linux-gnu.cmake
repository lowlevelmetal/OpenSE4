# Cross-compiling for 64-bit ARM Linux (aarch64, ARMv8-A), e.g. from x86_64:
#   cmake --preset dist-linux-aarch64
# GCC: Debian and Ubuntu package it as g++-aarch64-linux-gnu, Arch as
# aarch64-linux-gnu-gcc. The rest is in linux-cross.cmake.

set(CMAKE_SYSTEM_PROCESSOR aarch64)
set(OPENSE4_TRIPLE aarch64-linux-gnu)
set(OPENSE4_QEMU_ARCH aarch64)
include("${CMAKE_CURRENT_LIST_DIR}/linux-cross.cmake")
