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

# Fall back to the bundled test project so a fresh clone produces a running
# browser build without the user needing to point at an external project.
if(NOT HADES_WEB_ASSET_DIR OR NOT EXISTS "${HADES_WEB_ASSET_DIR}")
  set(HADES_WEB_ASSET_DIR "${CMAKE_SOURCE_DIR}/tests/test_project/.hades")
  message(STATUS "Hades web: using default sample project at ${HADES_WEB_ASSET_DIR}")
endif()

# Apply Emscripten-specific flags to the HadesRuntime target.
# Call this after the target is defined.
function(hades_configure_web_target target)
  target_link_options(${target} PRIVATE
    --use-port=emdawnwebgpu
    -sUSE_SDL=2
    -sALLOW_MEMORY_GROWTH=1
    -sMAX_WEBGL_VERSION=2
    -sASYNCIFY=0
    -sINITIAL_MEMORY=134217728
    -sSTACK_SIZE=5242880
    -sEXIT_RUNTIME=0
  )
  # Debug builds: surface meaningful messages instead of raw WASM traps.
  target_link_options(${target} PRIVATE
    "$<$<CONFIG:Debug>:-sASSERTIONS=1>"
    "$<$<CONFIG:Debug>:-sSAFE_HEAP=1>"
    "$<$<CONFIG:Debug>:-gsource-map>")

  # Embed game assets via --preload-file if an asset directory was specified.
  if(HADES_WEB_ASSET_DIR AND EXISTS "${HADES_WEB_ASSET_DIR}")
    target_link_options(${target} PRIVATE
      "SHELL:--preload-file ${HADES_WEB_ASSET_DIR}@/assets/.hades")
  endif()

  # Preload editor chrome (logo + icon font) so SDL_GetBasePath-based lookups
  # resolve inside MEMFS.
  set(_hades_logo "${CMAKE_SOURCE_DIR}/assets/logo.bmp")
  set(_hades_font "${CMAKE_SOURCE_DIR}/assets/fonts/fa-solid-900.ttf")
  if(EXISTS "${_hades_logo}")
    target_link_options(${target} PRIVATE
      "SHELL:--preload-file ${_hades_logo}@/assets/logo.bmp")
  endif()
  if(EXISTS "${_hades_font}")
    target_link_options(${target} PRIVATE
      "SHELL:--preload-file ${_hades_font}@/assets/fonts/fa-solid-900.ttf")
  endif()

  # Use the custom HTML shell template.
  set(SHELL_FILE "${CMAKE_SOURCE_DIR}/web/shell.html")
  if(EXISTS "${SHELL_FILE}")
    target_link_options(${target} PRIVATE
      "SHELL:--shell-file ${SHELL_FILE}")
    # CMake does not track --shell-file as an input; declare it explicitly so
    # the HTML regenerates when shell.html is edited.
    set_property(TARGET ${target} APPEND PROPERTY LINK_DEPENDS "${SHELL_FILE}")
  endif()

  # Produce .html output (Emscripten generates .html + .js + .wasm).
  set_target_properties(${target} PROPERTIES SUFFIX ".html")
endfunction()
