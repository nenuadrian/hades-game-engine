cmake_minimum_required(VERSION 3.20)
project(Hades LANGUAGES CXX)

set(HADES_CXX_STANDARD 20)

set(CMAKE_CXX_STANDARD ${HADES_CXX_STANDARD})
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(cmake/Dependencies.cmake)
include(cmake/WebPlatform.cmake)

set(HADES_NEURAL_ENGINE_DIR "${CMAKE_SOURCE_DIR}/lib/hades-neural-engine")

function(hades_configure_cxx_standard target visibility)
  set_target_properties(
    "${target}"
    PROPERTIES
      CXX_STANDARD ${HADES_CXX_STANDARD}
      CXX_STANDARD_REQUIRED ON
      CXX_EXTENSIONS OFF)
  target_compile_features("${target}" ${visibility} cxx_std_${HADES_CXX_STANDARD})
endfunction()

function(hades_stage_runtime_dependency target dependency)
  if(NOT WIN32 OR NOT TARGET "${target}" OR NOT TARGET "${dependency}")
    return()
  endif()

  get_target_property(dependency_type "${dependency}" TYPE)
  if(NOT dependency_type STREQUAL "SHARED_LIBRARY" AND NOT dependency_type STREQUAL "MODULE_LIBRARY")
    return()
  endif()

  add_custom_command(
    TARGET "${target}"
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${target}>"
    COMMAND
      ${CMAKE_COMMAND} -E copy_if_different
      "$<TARGET_FILE:${dependency}>"
      "$<TARGET_FILE_DIR:${target}>"
    VERBATIM)
endfunction()

if(NOT EMSCRIPTEN)
  find_package(Vulkan REQUIRED)
endif()
hades_configure_dependencies()

if(NOT EXISTS "${HADES_NEURAL_ENGINE_DIR}/CMakeLists.txt")
  message(
    FATAL_ERROR
      "Missing submodule '${HADES_NEURAL_ENGINE_DIR}'. "
      "Run 'git submodule update --init --recursive' and reconfigure.")
endif()

# Keep HNE integration lightweight until the engine starts linking against it.
set(HNE_BUILD_TRAINING OFF CACHE BOOL "Build Hades Neural Engine training targets.")
set(HNE_BUILD_INFERENCE OFF CACHE BOOL "Build Hades Neural Engine inference targets.")
set(HNE_BUILD_IMGUI_WIDGETS OFF CACHE BOOL "Build Hades Neural Engine ImGui widgets.")
set(HNE_BUILD_TESTS OFF CACHE BOOL "Build Hades Neural Engine tests.")
set(HNE_BUILD_EXAMPLES OFF CACHE BOOL "Build Hades Neural Engine examples.")
add_subdirectory("${HADES_NEURAL_ENGINE_DIR}")

configure_file(
  "${CMAKE_SOURCE_DIR}/cmake/build_config.hpp.in"
  "${CMAKE_BINARY_DIR}/generated/build_config.hpp"
  @ONLY)

# Define the source directory.
set(SRC_DIR "${CMAKE_SOURCE_DIR}/src")

# Collect engine sources (everything under src/engine/ except the editor-only
# ImGui GUI implementation which renders the dockspace and menu bar).
file(GLOB_RECURSE ENGINE_SOURCE_FILES CONFIGURE_DEPENDS "${SRC_DIR}/engine/*.cpp")
file(GLOB_RECURSE ENGINE_HEADER_FILES CONFIGURE_DEPENDS "${SRC_DIR}/engine/*.h" "${SRC_DIR}/engine/*.hpp")
list(REMOVE_ITEM ENGINE_SOURCE_FILES "${SRC_DIR}/engine/gui/imgui.cpp")

if(EMSCRIPTEN)
  # For web builds: remove Vulkan renderer and native scripting sources;
  # the WebGPU renderer is already picked up by the glob.
  list(REMOVE_ITEM ENGINE_SOURCE_FILES
    "${SRC_DIR}/engine/rendering/vulkan.cpp"
    "${SRC_DIR}/engine/runtime/script_runtime.cpp"
    "${SRC_DIR}/engine/runtime/script_compiler.cpp"
    "${SRC_DIR}/engine/runtime/script_loader.cpp"
    "${SRC_DIR}/engine/runtime/subprocess.cpp")
else()
  # For native builds: remove WebGPU renderer (only usable with Emscripten).
  list(REMOVE_ITEM ENGINE_SOURCE_FILES
    "${SRC_DIR}/engine/rendering/webgpu_renderer.cpp")
endif()

# Collect editor sources (src/editor/ plus the editor-only ImGui GUI and main.cpp).
file(GLOB_RECURSE EDITOR_SOURCE_FILES CONFIGURE_DEPENDS "${SRC_DIR}/editor/*.cpp")
file(GLOB_RECURSE EDITOR_HEADER_FILES CONFIGURE_DEPENDS "${SRC_DIR}/editor/*.h" "${SRC_DIR}/editor/*.hpp")
list(APPEND EDITOR_SOURCE_FILES "${SRC_DIR}/engine/gui/imgui.cpp" "${SRC_DIR}/main.cpp")

# Collect runtime sources.
file(GLOB_RECURSE RUNTIME_SOURCE_FILES CONFIGURE_DEPENDS "${SRC_DIR}/runtime/*.cpp")
file(GLOB_RECURSE RUNTIME_HEADER_FILES CONFIGURE_DEPENDS "${SRC_DIR}/runtime/*.hpp")

# Collect test sources.
file(GLOB_RECURSE TEST_SOURCE_FILES CONFIGURE_DEPENDS "${SRC_DIR}/tests/*.cpp")

option(HADES_FRAME_METRICS "Enable frame metrics profiling" OFF)
option(HADES_ENABLE_API "Enable HadesAPI REST server for ML training" OFF)

# ---------------------------------------------------------------------------
# HadesEngine static library (engine core, shared by editor and runtime)
# ---------------------------------------------------------------------------
add_library(HadesEngine STATIC ${ENGINE_SOURCE_FILES} ${ENGINE_HEADER_FILES})
hades_configure_cxx_standard(HadesEngine PUBLIC)
target_include_directories(HadesEngine PUBLIC "${SRC_DIR}" "${CMAKE_BINARY_DIR}/generated")
if(HADES_FRAME_METRICS)
  target_compile_definitions(HadesEngine PUBLIC HADES_ENABLE_FRAME_METRICS)
endif()
if(HADES_ENABLE_API AND NOT EMSCRIPTEN)
  target_compile_definitions(HadesEngine PUBLIC HADES_ENABLE_API)
  FetchContent_GetProperties(httplib)
  target_include_directories(HadesEngine PUBLIC "${httplib_SOURCE_DIR}")
endif()
if(EMSCRIPTEN)
  target_link_libraries(
    HadesEngine
    PUBLIC hades_imgui
           assimp
           Jolt
           miniaudio
           nlohmann_json::nlohmann_json)
else()
  target_link_libraries(
    HadesEngine
    PUBLIC hades_imgui
           assimp
           Jolt
           miniaudio
           nlohmann_json::nlohmann_json
           Vulkan::Vulkan)
  if(WIN32)
    target_link_libraries(HadesEngine PUBLIC Ole32 uuid)
  else()
    target_link_libraries(HadesEngine PUBLIC dl)
  endif()
endif()

# ---------------------------------------------------------------------------
# Hades editor executable (native builds only)
# ---------------------------------------------------------------------------
if(NOT EMSCRIPTEN)
add_executable(${PROJECT_NAME} ${EDITOR_SOURCE_FILES} ${EDITOR_HEADER_FILES})
hades_configure_cxx_standard(${PROJECT_NAME} PRIVATE)
target_include_directories(${PROJECT_NAME} PRIVATE "${CMAKE_BINARY_DIR}/generated")
target_link_libraries(
  ${PROJECT_NAME}
  PRIVATE HadesEngine
          CLI11::CLI11)
hades_stage_runtime_dependency(${PROJECT_NAME} assimp)

add_custom_command(
  TARGET ${PROJECT_NAME}
  POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${PROJECT_NAME}>/assets"
  COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${PROJECT_NAME}>/assets/fonts"
  COMMAND
    ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_SOURCE_DIR}/assets/logo.bmp"
    "$<TARGET_FILE_DIR:${PROJECT_NAME}>/assets/logo.bmp"
  COMMAND
    ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_SOURCE_DIR}/assets/fonts/fa-solid-900.ttf"
    "$<TARGET_FILE_DIR:${PROJECT_NAME}>/assets/fonts/fa-solid-900.ttf")

if(TARGET SDL2::SDL2main)
  target_link_libraries(${PROJECT_NAME} PRIVATE SDL2::SDL2main)
endif()
endif() # NOT EMSCRIPTEN (editor)

# ---------------------------------------------------------------------------
# HadesRuntime standalone game executable
# ---------------------------------------------------------------------------
add_executable(HadesRuntime ${RUNTIME_SOURCE_FILES} ${RUNTIME_HEADER_FILES})
hades_configure_cxx_standard(HadesRuntime PRIVATE)
target_include_directories(HadesRuntime PRIVATE "${CMAKE_BINARY_DIR}/generated")
if(EMSCRIPTEN)
  target_link_libraries(HadesRuntime PRIVATE HadesEngine)
  hades_configure_web_target(HadesRuntime)
else()
  target_link_libraries(
    HadesRuntime
    PRIVATE HadesEngine
            CLI11::CLI11)
  hades_stage_runtime_dependency(HadesRuntime assimp)

  add_custom_command(
    TARGET HadesRuntime
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:HadesRuntime>/assets"
    COMMAND
      ${CMAKE_COMMAND} -E copy_if_different
      "${CMAKE_SOURCE_DIR}/assets/logo.bmp"
      "$<TARGET_FILE_DIR:HadesRuntime>/assets/logo.bmp")

  if(TARGET SDL2::SDL2main)
    target_link_libraries(HadesRuntime PRIVATE SDL2::SDL2main)
  endif()
endif()

# ---------------------------------------------------------------------------
# Tests (native builds only)
# ---------------------------------------------------------------------------
if(NOT EMSCRIPTEN)
# Tests reference a few editor-specific sources (workspace manager, settings,
# script analysis, plugin registry) in addition to the engine library.
set(TEST_EDITOR_SUPPORT_SOURCES
  src/editor/editor_settings.cpp
  src/editor/plugins/editor_plugin.cpp
  src/editor/script_analysis.cpp
  src/editor/workspace_manager.cpp
  src/editor/workspace_file_operations.cpp)

add_executable(hades_tests ${TEST_SOURCE_FILES} ${TEST_EDITOR_SUPPORT_SOURCES})
hades_configure_cxx_standard(hades_tests PRIVATE)
target_include_directories(hades_tests PRIVATE "${SRC_DIR}" "${CMAKE_BINARY_DIR}/generated")
target_link_libraries(hades_tests PRIVATE HadesEngine gtest_main)
hades_stage_runtime_dependency(hades_tests assimp)

enable_testing()
include(GoogleTest)
if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.18)
  gtest_discover_tests(hades_tests DISCOVERY_MODE PRE_TEST)
else()
  gtest_discover_tests(hades_tests)
endif()
endif() # NOT EMSCRIPTEN (tests)
