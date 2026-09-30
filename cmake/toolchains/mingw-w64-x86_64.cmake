# Cross-compiling for 64-bit Windows with MinGW-w64 (GCC), e.g. from Linux:
#   cmake --preset dist-windows
# Host tools (glslc) come from the build machine; libraries and headers from the
# MinGW-w64 sysroot. Tests run through Wine when it is installed.

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(OPENSE4_MINGW_PREFIX x86_64-w64-mingw32)
set(CMAKE_C_COMPILER ${OPENSE4_MINGW_PREFIX}-gcc)
set(CMAKE_CXX_COMPILER ${OPENSE4_MINGW_PREFIX}-g++)
set(CMAKE_RC_COMPILER ${OPENSE4_MINGW_PREFIX}-windres)

set(CMAKE_FIND_ROOT_PATH /usr/${OPENSE4_MINGW_PREFIX})
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

find_program(OPENSE4_WINE wine)
if(OPENSE4_WINE)
    set(CMAKE_CROSSCOMPILING_EMULATOR ${OPENSE4_WINE})
endif()
