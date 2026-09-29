# Shader pipeline.
#
# Every shader is written once in backend-neutral GLSL without a #version line;
# `#ifdef VULKAN` selects push constants / descriptor bindings vs. plain
# uniforms. For each shader we generate a C++ header containing:
#   - SPIR-V compiled by glslc for the Vulkan backend
#   - the raw GLSL source for the OpenGL backend (compiled at runtime with a
#     "#version 330 core" prologue)

set(OPENSE4_EMBED_SCRIPT "${CMAKE_CURRENT_LIST_DIR}/EmbedFiles.cmake")

# opense4_add_shaders(<target> <shader files...>)
# Shader stage is taken from the extension: foo.vert / foo.frag.
function(opense4_add_shaders target)
    set(out_dir "${CMAKE_CURRENT_BINARY_DIR}/generated/shaders")
    file(MAKE_DIRECTORY "${out_dir}")
    set(headers "")
    foreach(src IN LISTS ARGN)
        get_filename_component(abs "${src}" ABSOLUTE)
        get_filename_component(name "${src}" NAME)
        string(REPLACE "." "_" ident "${name}")
        set(spv "${out_dir}/${name}.spv")
        set(hdr "${out_dir}/${ident}.h")
        add_custom_command(
            OUTPUT "${spv}"
            COMMAND "${OPENSE4_GLSLC}" -std=450core --target-env=vulkan1.3 -O -Werror
                    -MD -MF "${spv}.d" -o "${spv}" "${abs}"
            DEPENDS "${abs}"
            DEPFILE "${spv}.d"
            COMMENT "Compiling shader ${name}"
            VERBATIM)
        add_custom_command(
            OUTPUT "${hdr}"
            COMMAND "${CMAKE_COMMAND}"
                    "-DOUTPUT=${hdr}" "-DIDENT=${ident}"
                    "-DSPIRV=${spv}" "-DSOURCE=${abs}"
                    -P "${OPENSE4_EMBED_SCRIPT}"
            DEPENDS "${spv}" "${abs}" "${OPENSE4_EMBED_SCRIPT}"
            COMMENT "Embedding shader ${name}"
            VERBATIM)
        list(APPEND headers "${hdr}")
    endforeach()
    target_sources(${target} PRIVATE ${headers})
    target_include_directories(${target} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
endfunction()
