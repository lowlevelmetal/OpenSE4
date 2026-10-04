# Cross-compiling for 32-bit ARM Linux (armhf), e.g. from x86_64:
#   cmake --preset dist-linux-armhf
# GCC: Debian and Ubuntu package it as g++-arm-linux-gnueabihf. The rest is in
# linux-cross.cmake.
#
# The code is for ARMv7-A with the hard-float ABI, VFPv3 and NEON: every
# Raspberry Pi from the Pi 2 on (Raspberry Pi OS 32-bit runs it, though its own
# packages are built for ARMv6), and the ARMv7 and ARMv8 boards that run a
# 32-bit Debian or Ubuntu. VFPv3 rather than VFPv4 keeps the Cortex-A8 and A9 in;
# VFPv4 would add only fused multiply-add, which the game never uses
# (docs/ENGINE.md, "Same on every platform"). The ARMv6 Pi 1 and Pi Zero are
# left out.

set(CMAKE_SYSTEM_PROCESSOR armv7l)
set(OPENSE4_TRIPLE arm-linux-gnueabihf)
set(OPENSE4_QEMU_ARCH arm)
set(CMAKE_C_FLAGS_INIT "-march=armv7-a -mfpu=neon -mfloat-abi=hard")
set(CMAKE_CXX_FLAGS_INIT "-march=armv7-a -mfpu=neon -mfloat-abi=hard")
include("${CMAKE_CURRENT_LIST_DIR}/linux-cross.cmake")
