# Linux release builds (OPENSE4_STATIC): run on glibc 2.34 and later even when built
# on a newer distribution. Each of our executables links src/compat/glibc_compat*.c
# and the linker sends calls to the functions below to their wrappers there
# (--wrap). tools/check_glibc.sh reports what a built binary still requires.

set(OPENSE4_GLIBC_WRAPPED
    acosf asinf atan2f log10f sqrtf hypotf fmod fmodf
    __isoc23_strtol __isoc23_strtoll __isoc23_strtoul __isoc23_strtoull __isoc23_wcstol
    __isoc23_sscanf __isoc23_fscanf __isoc23_vsscanf
    strlcpy strlcat wcslcpy wcslcat
    arc4random _dl_find_object)

add_library(opense4_glibc_compat OBJECT
    "${CMAKE_SOURCE_DIR}/src/compat/glibc_compat.c"
    "${CMAKE_SOURCE_DIR}/src/compat/glibc_compat_dlfo.c")
set_target_properties(opense4_glibc_compat PROPERTIES C_STANDARD 11 C_EXTENSIONS OFF)
target_compile_options(opense4_glibc_compat PRIVATE -fno-builtin)

# opense4_apply_glibc_compat(<dir>...): every executable defined in those source
# directories (and below) gets the wrappers.
function(opense4_apply_glibc_compat)
    set(dirs ${ARGN})
    while(dirs)
        list(POP_FRONT dirs dir)
        get_property(sub DIRECTORY "${dir}" PROPERTY SUBDIRECTORIES)
        list(APPEND dirs ${sub})
        get_property(targets DIRECTORY "${dir}" PROPERTY BUILDSYSTEM_TARGETS)
        foreach(t IN LISTS targets)
            get_target_property(type ${t} TYPE)
            if(type STREQUAL "EXECUTABLE")
                target_link_libraries(${t} PRIVATE opense4_glibc_compat)
                foreach(fn IN LISTS OPENSE4_GLIBC_WRAPPED)
                    target_link_options(${t} PRIVATE "LINKER:--wrap=${fn}")
                endforeach()
            endif()
        endforeach()
    endwhile()
endfunction()
