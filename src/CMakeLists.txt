# --- core: math, rng, ids, logging (no dependencies) -----------------------------
add_library(opense4_core STATIC
    core/environment.cpp
    core/log.cpp)
target_include_directories(opense4_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(opense4_core PRIVATE opense4_warnings)

# --- script: the modding SDK's values and script runtime (docs/MODDING_SDK.md) ---------
# MicroPython (cmake/MicroPython.cmake) with our port (script/port), the conversions,
# JSON, and the runtime's own Python modules (python/lib), always built in.
include(MicroPython)
file(GLOB_RECURSE script_library RELATIVE "${CMAKE_SOURCE_DIR}/python/lib" CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/python/lib/*.py")
list(SORT script_library)
list(TRANSFORM script_library PREPEND "${CMAKE_SOURCE_DIR}/python/lib/" OUTPUT_VARIABLE script_library_paths)
list(JOIN script_library "|" script_library_arg)
set(script_library_cpp "${CMAKE_CURRENT_BINARY_DIR}/generated/script_library.cpp")
add_custom_command(
    OUTPUT "${script_library_cpp}"
    COMMAND "${CMAKE_COMMAND}" "-DOUTPUT=${script_library_cpp}" "-DROOT=${CMAKE_SOURCE_DIR}/python/lib"
            "-DFILES=${script_library_arg}" -P "${CMAKE_SOURCE_DIR}/cmake/EmbedPython.cmake"
    DEPENDS ${script_library_paths} "${CMAKE_SOURCE_DIR}/cmake/EmbedPython.cmake"
    COMMENT "Embedding the script runtime's Python modules"
    VERBATIM)
# The port is our C code: our warnings, in their C form on MSVC (/permissive- is C++).
add_library(opense4_script_port OBJECT script/port/port.c)
target_link_libraries(opense4_script_port PRIVATE opense4_micropython)
set_target_properties(opense4_script_port PROPERTIES C_STANDARD 11)
if(MSVC)
    target_compile_options(opense4_script_port PRIVATE /W4 /utf-8 $<$<BOOL:${OPENSE4_WARNINGS_AS_ERRORS}>:/WX>)
    target_compile_definitions(opense4_script_port PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    target_link_libraries(opense4_script_port PRIVATE opense4_warnings)
endif()
add_library(opense4_script STATIC
    script/json.cpp
    script/runtime.cpp
    script/value.cpp
    "${script_library_cpp}"
    $<TARGET_OBJECTS:opense4_script_port>)
target_include_directories(opense4_script PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(opense4_script PRIVATE opense4_micropython opense4_warnings)
if(OPENSE4_ASAN_STACK_OPTION)
    # Python objects held in the runtime's own frames must be on the real stack
    # (cmake/MicroPython.cmake).
    target_compile_options(opense4_script PRIVATE ${OPENSE4_ASAN_STACK_OPTION})
    target_compile_options(opense4_script_port PRIVATE ${OPENSE4_ASAN_STACK_OPTION})
endif()

# --- embedded: our own fonts and learning content built into the executable ---------
# (core/embedded.hpp). Only our own files: nothing from the original game.
if(OPENSE4_EMBED_RESOURCES)
    file(GLOB embedded_files RELATIVE "${CMAKE_SOURCE_DIR}" CONFIGURE_DEPENDS
        "${CMAKE_SOURCE_DIR}/assets/fonts/*.ttf")
    # Manual pages, tutorials and training games (docs/LEARNING.md).
    file(GLOB_RECURSE embedded_learn RELATIVE "${CMAKE_SOURCE_DIR}" CONFIGURE_DEPENDS
        "${CMAKE_SOURCE_DIR}/assets/learn/*.md"
        "${CMAKE_SOURCE_DIR}/assets/learn/*.toml")
    list(APPEND embedded_files ${embedded_learn})
    list(SORT embedded_files)
    list(TRANSFORM embedded_files PREPEND "${CMAKE_SOURCE_DIR}/" OUTPUT_VARIABLE embedded_paths)
    list(JOIN embedded_files "|" embedded_arg)
    set(embedded_cpp "${CMAKE_CURRENT_BINARY_DIR}/generated/embedded_resources.cpp")
    add_custom_command(
        OUTPUT "${embedded_cpp}"
        COMMAND "${CMAKE_COMMAND}" "-DOUTPUT=${embedded_cpp}" "-DROOT=${CMAKE_SOURCE_DIR}" "-DFILES=${embedded_arg}"
                -P "${CMAKE_SOURCE_DIR}/cmake/EmbedResources.cmake"
        DEPENDS ${embedded_paths} "${CMAKE_SOURCE_DIR}/cmake/EmbedResources.cmake"
        COMMENT "Embedding fonts and learning content"
        VERBATIM)
    add_library(opense4_embedded STATIC "${embedded_cpp}")
else()
    add_library(opense4_embedded STATIC core/embedded_none.cpp)
endif()
target_link_libraries(opense4_embedded PUBLIC opense4_core)

# --- datafile + ruleset: the classic "Key := Value" data format and its typed model ---
add_library(opense4_datafile STATIC
    datafile/datafile.cpp
    datafile/reader.cpp)
target_include_directories(opense4_datafile PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(opense4_datafile PRIVATE opense4_warnings)

add_library(opense4_ruleset STATIC
    ruleset/files.cpp
    ruleset/load.cpp
    ruleset/mods.cpp
    ruleset/races.cpp
    ruleset/ruleset.cpp)
target_link_libraries(opense4_ruleset PUBLIC opense4_core opense4_datafile PRIVATE opense4_warnings)
if(WIN32)
    target_link_libraries(opense4_ruleset PRIVATE advapi32)  # the Steam folder from the registry
endif()

# --- game: the classic rules engine ------------------------------------------------
add_library(opense4_game STATIC
    game/abilities.cpp
    game/ai.cpp
    game/ai_anger.cpp
    game/ai_data.cpp
    game/ai_design.cpp
    game/ai_diplomacy.cpp
    game/ai_economy.cpp
    game/ai_explore.cpp
    game/ai_military.cpp
    game/ai_research.cpp
    game/classic_save_checksums.cpp
    game/classic_save_codec.cpp
    game/classic_save_export.cpp
    game/classic_save_import.cpp
    game/classic_save_tables.cpp
    game/combat.cpp
    game/combat_ground.cpp
    game/combat_space.cpp
    game/combat_tactical.cpp
    game/commands.cpp
    game/design.cpp
    game/diplomacy.cpp
    game/economy.cpp
    game/economy_population.cpp
    game/economy_queue.cpp
    game/events.cpp
    game/generate.cpp
    game/intel.cpp
    game/log_picture.cpp
    game/map_file.cpp
    game/movement.cpp
    game/movement_path.cpp
    game/movement_route.cpp
    game/movement_stellar.cpp
    game/movement_upkeep.cpp
    game/movement_util.cpp
    game/orders.cpp
    game/players.cpp
    game/query.cpp
    game/redact.cpp
    game/research.cpp
    game/rules.cpp
    game/scrap.cpp
    game/score.cpp
    game/serialize.cpp
    game/setup.cpp
    game/simulator.cpp
    game/sight.cpp
    game/state.cpp
    game/turn.cpp
    game/turn_based.cpp
    game/types.cpp)
target_link_libraries(opense4_game PUBLIC opense4_ruleset PRIVATE tomlplusplus opense4_warnings)

# --- mods: packages, mod sets, the layered game files and data patches -------------
# (docs/MODDING_SDK.md, docs/sdk/packages-and-data.md)
add_library(opense4_mods STATIC
    mods/apply.cpp
    mods/data_set.cpp
    mods/generator.cpp
    mods/manifest.cpp
    mods/mod_set.cpp
    mods/package.cpp
    mods/patch.cpp
    mods/tables.cpp
    mods/version.cpp
    mods/zip.cpp)
target_link_libraries(opense4_mods PUBLIC opense4_game opense4_script PRIVATE tomlplusplus miniz monocypher opense4_warnings)

# --- sdk: the engine's side of the modding SDK (docs/MODDING_SDK.md) ---------------
find_package(Threads REQUIRED)
# The view scripts read, their queries and the command codec, on script::Value;
# the computer players' sessions, with the `opense4` package (python/opense4)
# built in.
file(GLOB_RECURSE sdk_package RELATIVE "${CMAKE_SOURCE_DIR}/python" CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/python/opense4/*.py")
list(SORT sdk_package)
list(TRANSFORM sdk_package PREPEND "${CMAKE_SOURCE_DIR}/python/" OUTPUT_VARIABLE sdk_package_paths)
list(JOIN sdk_package "|" sdk_package_arg)
set(sdk_package_cpp "${CMAKE_CURRENT_BINARY_DIR}/generated/sdk_package.cpp")
add_custom_command(
    OUTPUT "${sdk_package_cpp}"
    COMMAND "${CMAKE_COMMAND}" "-DOUTPUT=${sdk_package_cpp}" "-DROOT=${CMAKE_SOURCE_DIR}/python"
            "-DFILES=${sdk_package_arg}" "-DHEADER=sdk/players.hpp" "-DNAMESPACE=opense4::sdk"
            "-DFUNCTION=packageFiles" "-DORIGIN=python/opense4" -P "${CMAKE_SOURCE_DIR}/cmake/EmbedPython.cmake"
    DEPENDS ${sdk_package_paths} "${CMAKE_SOURCE_DIR}/cmake/EmbedPython.cmake"
    COMMENT "Embedding the opense4 Python package"
    VERBATIM)
add_library(opense4_sdk STATIC
    sdk/codec.cpp
    sdk/names.cpp
    sdk/parts.cpp
    sdk/player_check.cpp
    sdk/player_values.cpp
    sdk/players.cpp
    sdk/queries.cpp
    sdk/rules_view.cpp
    sdk/view.cpp
    sdk/worker.cpp
    "${sdk_package_cpp}")
target_link_libraries(opense4_sdk PUBLIC opense4_game opense4_mods opense4_script PRIVATE Threads::Threads opense4_warnings)

# --- learn: tutorials, training games and the manual (docs/LEARNING.md) --------------
# Headless: the content model, its loaders and the condition evaluator. The
# client draws it; the tests check the built-in content with it.
add_library(opense4_learn STATIC
    learn/access.cpp
    learn/condition.cpp
    learn/ids.cpp
    learn/lesson.cpp
    learn/library.cpp
    learn/markdown.cpp
    learn/progress.cpp
    learn/resume.cpp
    learn/tokens.cpp)
target_link_libraries(opense4_learn PUBLIC opense4_game PRIVATE tomlplusplus opense4_warnings)

# --- net: multiplayer (TCP host/client sessions, UPnP port mapping, PBEM files) -----
find_package(Threads REQUIRED)
add_library(opense4_net STATIC
    net/auth.cpp
    net/client.cpp
    net/connection.cpp
    net/crypto.cpp
    net/host.cpp
    net/pbem.cpp
    net/protocol.cpp
    net/secure.cpp
    net/socket.cpp
    net/upnp.cpp
    net/discovery.cpp)
target_link_libraries(opense4_net PUBLIC opense4_game PRIVATE Threads::Threads monocypher opense4_warnings)
target_compile_definitions(opense4_net PRIVATE OPENSE4_VERSION="${PROJECT_VERSION}")
if(OPENSE4_ENABLE_UPNP)
    target_sources(opense4_net PRIVATE net/upnp_miniupnpc.cpp)
    target_link_libraries(opense4_net PRIVATE miniupnpc::miniupnpc)
else()
    target_sources(opense4_net PRIVATE net/upnp_disabled.cpp)
endif()
if(WIN32)
    target_link_libraries(opense4_net PRIVATE ws2_32 bcrypt)
endif()

# --- the dedicated server (network games and play-by-e-mail) ------------------------
add_executable(opense4-server
    server/main.cpp
    server/setup_file.cpp)
target_link_libraries(opense4-server PRIVATE opense4_net opense4_mods opense4_game tomlplusplus opense4_warnings)
set_target_properties(opense4-server PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
opense4_windows_manifest(opense4-server)

# --- assets: runtime access to the player's installed classic art ------------------
add_library(opense4_assets STATIC
    assets/assets.cpp
    assets/tiny_font.cpp
    assets/wincursor.cpp
    assets/winfont.cpp)
target_link_libraries(opense4_assets PUBLIC opense4_core PRIVATE stb opense4_warnings)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/gfx/device.hpp")
    include(Client.cmake)
endif()
