add_executable(opense4-datacheck datacheck.cpp)
target_link_libraries(opense4-datacheck PRIVATE opense4_ruleset opense4_warnings)
set_target_properties(opense4-datacheck PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
opense4_windows_manifest(opense4-datacheck)

# The original's saved games and OpenSE4's, both ways (docs/SETUP.md).
add_executable(opense4-convert convert.cpp)
target_link_libraries(opense4-convert PRIVATE opense4_game opense4_warnings)
set_target_properties(opense4-convert PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
opense4_windows_manifest(opense4-convert)

# The modding SDK's tool: new, check, dump, pack and info (docs/sdk/packages-and-data.md).
add_executable(opense4-sdk sdk.cpp)
target_link_libraries(opense4-sdk PRIVATE opense4_sdk opense4_mods opense4_net opense4_assets opense4_warnings)
set_target_properties(opense4-sdk PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
opense4_windows_manifest(opense4-sdk)

# Black-box observation of the original game under X11/XWayland (Linux dev tool).
find_package(X11 QUIET)
if(X11_FOUND AND X11_XTest_FOUND)
    add_executable(opense4-observe observe/observe.cpp)
    target_link_libraries(opense4-observe PRIVATE X11::X11 X11::Xtst)
    set_target_properties(opense4-observe PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
endif()
