# The Windows release toolchain (the dist-windows preset): llvm-mingw, MinGW-w64 with
# Clang, LLD and libc++, in its build for msvcrt.dll. msvcrt.dll is part of every
# Windows version, so the programs run on Windows 7 SP1 to 11 as they are. The
# Universal C Runtime that other MinGW-w64 builds use is missing from Windows 7
# until its update KB2999226 (docs/BUILDING.md, "Windows 7 to 11"). llvm-mingw's
# headers and runtime libraries target Windows 7.
#
# A pinned release is downloaded once into build/_tools and checked against its
# SHA-256. LLVM_MINGW_DIR (environment or cache variable) names another copy of
# the same build instead. The toolchain runs on Linux x86_64 (a cross build; the
# tests run through Wine when it is installed) and on Windows x64.

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

# The release, with the SHA-256 of each host's archive as published with it.
set(OPENSE4_LLVM_MINGW_RELEASE 20260922)  # LLVM 23.1.2, MinGW-w64 15.0
if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64)$")
    set(_opense4_llvm_mingw llvm-mingw-${OPENSE4_LLVM_MINGW_RELEASE}-msvcrt-ubuntu-22.04-x86_64)
    set(_opense4_llvm_mingw_archive ${_opense4_llvm_mingw}.tar.xz)
    set(_opense4_llvm_mingw_sha256 17bbc667b96f7b1f02ce4a172d8cac4fa480927a1f649e788dee5914919566a1)
    set(_opense4_exe "")
elseif(CMAKE_HOST_WIN32 AND CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64)$")
    set(_opense4_llvm_mingw llvm-mingw-${OPENSE4_LLVM_MINGW_RELEASE}-msvcrt-x86_64)
    set(_opense4_llvm_mingw_archive ${_opense4_llvm_mingw}.zip)
    set(_opense4_llvm_mingw_sha256 1e936a4a694fc27f9625e3311f5ec5d6d99abfeaa514fd41c52a4f8347145d1a)
    set(_opense4_exe ".exe")
else()
    message(FATAL_ERROR "The Windows release toolchain (llvm-mingw for msvcrt.dll) is built for Linux x86_64 "
                        "and Windows x64 hosts, not ${CMAKE_HOST_SYSTEM_NAME} ${CMAKE_HOST_SYSTEM_PROCESSOR}.")
endif()

if(NOT LLVM_MINGW_DIR AND DEFINED ENV{LLVM_MINGW_DIR})
    set(LLVM_MINGW_DIR "$ENV{LLVM_MINGW_DIR}")
endif()
if(NOT LLVM_MINGW_DIR)
    get_filename_component(_opense4_tools "${CMAKE_CURRENT_LIST_DIR}/../../build/_tools" ABSOLUTE)
    set(LLVM_MINGW_DIR "${_opense4_tools}/${_opense4_llvm_mingw}")
    if(NOT EXISTS "${LLVM_MINGW_DIR}/bin/x86_64-w64-mingw32-clang++${_opense4_exe}")
        set(_opense4_url "https://github.com/mstorsjo/llvm-mingw/releases/download/${OPENSE4_LLVM_MINGW_RELEASE}/${_opense4_llvm_mingw_archive}")
        message(STATUS "Fetching ${_opense4_llvm_mingw_archive} into ${_opense4_tools}")
        file(DOWNLOAD "${_opense4_url}" "${_opense4_tools}/${_opense4_llvm_mingw_archive}"
            EXPECTED_HASH SHA256=${_opense4_llvm_mingw_sha256}
            STATUS _opense4_status)
        list(GET _opense4_status 0 _opense4_code)
        if(NOT _opense4_code EQUAL 0)
            file(REMOVE "${_opense4_tools}/${_opense4_llvm_mingw_archive}")
            message(FATAL_ERROR "Could not fetch ${_opense4_url}: ${_opense4_status}")
        endif()
        # Unpacked beside its final place and then moved, so a broken unpack leaves
        # no half toolchain behind.
        file(REMOVE_RECURSE "${_opense4_tools}/.unpack" "${LLVM_MINGW_DIR}")
        file(ARCHIVE_EXTRACT INPUT "${_opense4_tools}/${_opense4_llvm_mingw_archive}" DESTINATION "${_opense4_tools}/.unpack")
        file(RENAME "${_opense4_tools}/.unpack/${_opense4_llvm_mingw}" "${LLVM_MINGW_DIR}")
        file(REMOVE_RECURSE "${_opense4_tools}/.unpack")
        file(REMOVE "${_opense4_tools}/${_opense4_llvm_mingw_archive}")
    endif()
endif()
if(NOT EXISTS "${LLVM_MINGW_DIR}/bin/x86_64-w64-mingw32-clang++${_opense4_exe}")
    message(FATAL_ERROR "No llvm-mingw toolchain in ${LLVM_MINGW_DIR}")
endif()
# Kept for the try_compile projects CMake configures with this file.
set(LLVM_MINGW_DIR "${LLVM_MINGW_DIR}" CACHE PATH "llvm-mingw (msvcrt build) for the Windows release toolchain")
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES LLVM_MINGW_DIR)

set(CMAKE_C_COMPILER "${LLVM_MINGW_DIR}/bin/x86_64-w64-mingw32-clang${_opense4_exe}")
set(CMAKE_CXX_COMPILER "${LLVM_MINGW_DIR}/bin/x86_64-w64-mingw32-clang++${_opense4_exe}")
set(CMAKE_RC_COMPILER "${LLVM_MINGW_DIR}/bin/x86_64-w64-mingw32-windres${_opense4_exe}")

set(CMAKE_FIND_ROOT_PATH "${LLVM_MINGW_DIR}/x86_64-w64-mingw32")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

if(NOT CMAKE_HOST_WIN32)
    find_program(OPENSE4_WINE wine)
    if(OPENSE4_WINE)
        set(CMAKE_CROSSCOMPILING_EMULATOR ${OPENSE4_WINE})
    endif()
endif()
