include_guard(GLOBAL)

# Web platform support via Emscripten.
# This file is only effective when building with the Emscripten toolchain
# (i.e. via `emcmake cmake`).

if(NOT EMSCRIPTEN)
  return()
endif()

message(STATUS "Emscripten detected -- configuring web build")

add_compile_definitions(HADES_PLATFORM_WEB)

set(HADES_WEB_ASSET_DIR "" CACHE PATH
    "Path to the game project directory whose .hades/ folder will be embedded into the web build.")

# Apply Emscripten-specific flags to the HadesRuntime target.
# Call this after the target is defined.
function(hades_configure_web_target target)
  target_link_options(${target} PRIVATE
    --use-port=emdawnwebgpu
    -sUSE_SDL=2
    -sALLOW_MEMORY_GROWTH=1
    -sMAX_WEBGL_VERSION=2
    -sASYNCIFY=0
    -sINITIAL_MEMORY=67108864
  )

  # Embed game assets via --preload-file if an asset directory was specified.
  if(HADES_WEB_ASSET_DIR AND EXISTS "${HADES_WEB_ASSET_DIR}")
    target_link_options(${target} PRIVATE
      "SHELL:--preload-file ${HADES_WEB_ASSET_DIR}@/assets/.hades")
  endif()

  # Use the custom HTML shell template.
  set(SHELL_FILE "${CMAKE_SOURCE_DIR}/web/shell.html")
  if(EXISTS "${SHELL_FILE}")
    target_link_options(${target} PRIVATE
      "SHELL:--shell-file ${SHELL_FILE}")
  endif()

  # Produce .html output (Emscripten generates .html + .js + .wasm).
  set_target_properties(${target} PROPERTIES SUFFIX ".html")
endfunction()
