# Windows builds with libc++ (the llvm-mingw release toolchain of the dist-windows
# preset): std::filesystem converts narrow strings as UTF-8, as libstdc++ does, also
# on Windows 7 to 10 1809, which ignore the manifest's UTF-8 code page. Each of our
# executables links src/compat/libcxx_utf8_paths.cpp and the linker sends libc++'s
# two conversion functions to it (--wrap). test_platform.cpp checks the wiring.
# Other Windows toolchains need nothing: libstdc++ converts as UTF-8 itself, and the
# MSVC builds rely on the manifest (Windows 10 1903 and later).

include(CheckCXXSourceCompiles)
check_cxx_source_compiles("
#include <version>
#ifndef _LIBCPP_VERSION
#error not libc++
#endif
int main() { return 0; }" OPENSE4_HAVE_LIBCXX)

set(OPENSE4_LIBCXX_PATH_FUNCTIONS
    _ZNSt3__14__fs10filesystem14__char_to_wideERKNS_12basic_stringIcNS_11char_traitsIcEENS_9allocatorIcEEEEPwy
    _ZNSt3__14__fs10filesystem14__wide_to_charERKNS_12basic_stringIwNS_11char_traitsIwEENS_9allocatorIwEEEEPcy)

if(OPENSE4_HAVE_LIBCXX)
    add_library(opense4_libcxx_utf8_paths OBJECT "${CMAKE_SOURCE_DIR}/src/compat/libcxx_utf8_paths.cpp")
    target_include_directories(opense4_libcxx_utf8_paths PRIVATE "${CMAKE_SOURCE_DIR}/src")
    target_link_libraries(opense4_libcxx_utf8_paths PRIVATE opense4_warnings)
endif()

# opense4_apply_windows_compat(<dir>...): every executable defined in those source
# directories (and below) gets the conversions.
function(opense4_apply_windows_compat)
    if(NOT OPENSE4_HAVE_LIBCXX)
        return()
    endif()
    set(dirs ${ARGN})
    while(dirs)
        list(POP_FRONT dirs dir)
        get_property(sub DIRECTORY "${dir}" PROPERTY SUBDIRECTORIES)
        list(APPEND dirs ${sub})
        get_property(targets DIRECTORY "${dir}" PROPERTY BUILDSYSTEM_TARGETS)
        foreach(t IN LISTS targets)
            get_target_property(type ${t} TYPE)
            if(type STREQUAL "EXECUTABLE")
                target_link_libraries(${t} PRIVATE opense4_libcxx_utf8_paths)
                target_compile_definitions(${t} PRIVATE OPENSE4_LIBCXX_UTF8_PATHS)
                foreach(fn IN LISTS OPENSE4_LIBCXX_PATH_FUNCTIONS)
                    target_link_options(${t} PRIVATE "LINKER:--wrap=${fn}")
                endforeach()
            endif()
        endforeach()
    endwhile()
endfunction()
