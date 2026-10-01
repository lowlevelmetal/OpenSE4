# CI helper (.github/workflows/ci.yml): keeps the sources that FetchContent
# downloads in a directory that actions/cache carries from run to run.
#
#   cmake -DDIR=.deps -P fetchcontent_cache.cmake
#       Writes DIR/sources.cmake, an initial cache for `cmake -C` that points
#       FETCHCONTENT_SOURCE_DIR_<NAME> at every source in DIR. With an empty DIR
#       the file is empty and everything is downloaded as usual.
#
#   cmake -DDIR=.deps -DBUILD=build/debug -P fetchcontent_cache.cmake
#       After a configure that downloaded them: copies BUILD/_deps/*-src into DIR.
#
# The cache key is the hash of cmake/Dependencies.cmake, so a changed pin starts
# from a fresh download.

if(NOT DEFINED DIR)
    message(FATAL_ERROR "Pass -DDIR=<cache directory>")
endif()
get_filename_component(dir "${DIR}" ABSOLUTE)
file(GLOB cached LIST_DIRECTORIES true "${dir}/*-src")

if(DEFINED BUILD)
    if(cached)
        return()  # restored from the cache: nothing new
    endif()
    file(GLOB sources LIST_DIRECTORIES true "${BUILD}/_deps/*-src")
    file(MAKE_DIRECTORY "${dir}")
    foreach(src IN LISTS sources)
        file(COPY "${src}" DESTINATION "${dir}")
    endforeach()
    list(LENGTH sources count)
    message(STATUS "Saved ${count} FetchContent sources in ${dir}")
    return()
endif()

set(lines "")
foreach(src IN LISTS cached)
    get_filename_component(name "${src}" NAME)
    string(REGEX REPLACE "-src$" "" name "${name}")
    string(TOUPPER "${name}" name)
    string(APPEND lines "set(FETCHCONTENT_SOURCE_DIR_${name} \"${src}\" CACHE PATH \"\")\n")
    message(STATUS "Cached source: ${name}")
endforeach()
file(MAKE_DIRECTORY "${dir}")
file(WRITE "${dir}/sources.cmake" "${lines}")
