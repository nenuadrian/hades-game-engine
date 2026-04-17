# Shaders.cmake — compiles GLSL shaders to SPIR-V and embeds them as C++
# headers. Consumed by HadesEngine's Vulkan mesh pipeline.

find_program(HADES_GLSLANG_VALIDATOR
  NAMES glslangValidator glslang
  HINTS
    "$ENV{VULKAN_SDK}/bin"
    "$ENV{VULKAN_SDK}/Bin"
    "$ENV{VULKAN_SDK}/Bin32")

if(NOT HADES_GLSLANG_VALIDATOR)
  message(WARNING
    "glslangValidator not found. Vulkan mesh rendering will be disabled. "
    "Install the Vulkan SDK or set VULKAN_SDK to enable.")
endif()

set(HADES_SHADER_OUT_DIR "${CMAKE_BINARY_DIR}/generated/shaders")
file(MAKE_DIRECTORY "${HADES_SHADER_OUT_DIR}")

# hades_add_shader(<glsl_input> <stage> <symbol_name>)
# Compiles GLSL -> SPIR-V -> embeds as C++ header. Appends paths to the
# target-level list HADES_SHADER_HEADERS so callers can add them as sources.
function(hades_add_shader glsl_input stage symbol)
  if(NOT HADES_GLSLANG_VALIDATOR)
    return()
  endif()
  get_filename_component(shader_name "${glsl_input}" NAME_WE)
  set(spv_out "${HADES_SHADER_OUT_DIR}/${shader_name}.${stage}.spv")
  set(hpp_out "${HADES_SHADER_OUT_DIR}/${shader_name}.${stage}.spv.hpp")

  add_custom_command(
    OUTPUT "${spv_out}"
    COMMAND "${HADES_GLSLANG_VALIDATOR}"
            -V -S "${stage}"
            "${glsl_input}" -o "${spv_out}"
    DEPENDS "${glsl_input}"
    COMMENT "Compiling shader ${shader_name}.${stage}"
    VERBATIM)

  add_custom_command(
    OUTPUT "${hpp_out}"
    COMMAND "${CMAKE_COMMAND}"
            -DINPUT=${spv_out}
            -DOUTPUT=${hpp_out}
            -DSYMBOL=${symbol}
            -P "${CMAKE_SOURCE_DIR}/cmake/EmbedSpirv.cmake"
    DEPENDS "${spv_out}" "${CMAKE_SOURCE_DIR}/cmake/EmbedSpirv.cmake"
    COMMENT "Embedding shader ${shader_name}.${stage}"
    VERBATIM)

  set(HADES_SHADER_HEADERS ${HADES_SHADER_HEADERS} "${hpp_out}" PARENT_SCOPE)
endfunction()
