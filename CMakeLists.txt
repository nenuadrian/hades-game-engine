cmake_minimum_required(VERSION 3.16)
project(HadesGameEngine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(cmake/Dependencies.cmake)

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

find_package(Vulkan REQUIRED)
hades_configure_dependencies()

set(HADES_DOTNET_EXECUTABLE "" CACHE FILEPATH "Absolute path to the dotnet executable used by runtime scripting")
set(HADES_DOTNET_ROOT "" CACHE PATH "Absolute path to DOTNET_ROOT used by runtime scripting")

set(HADES_DOTNET_EXECUTABLE_PATH "${HADES_DOTNET_EXECUTABLE}")
if(NOT HADES_DOTNET_EXECUTABLE_PATH)
  find_program(HADES_DOTNET_EXECUTABLE_PATH NAMES dotnet)
  if(HADES_DOTNET_EXECUTABLE_PATH)
    set(HADES_DOTNET_EXECUTABLE "${HADES_DOTNET_EXECUTABLE_PATH}" CACHE FILEPATH "Absolute path to the dotnet executable used by runtime scripting" FORCE)
  endif()
endif()

if(NOT HADES_DOTNET_ROOT AND HADES_DOTNET_EXECUTABLE_PATH)
  get_filename_component(HADES_DOTNET_ROOT_PATH "${HADES_DOTNET_EXECUTABLE_PATH}" DIRECTORY)
  set(HADES_DOTNET_ROOT "${HADES_DOTNET_ROOT_PATH}" CACHE PATH "Absolute path to DOTNET_ROOT used by runtime scripting" FORCE)
endif()

set(HADES_DOTNET_EXECUTABLE_CONFIG "${HADES_DOTNET_EXECUTABLE}")
set(HADES_DOTNET_ROOT_CONFIG "${HADES_DOTNET_ROOT}")
if(HADES_DOTNET_EXECUTABLE_CONFIG)
  file(TO_CMAKE_PATH "${HADES_DOTNET_EXECUTABLE_CONFIG}" HADES_DOTNET_EXECUTABLE_CONFIG)
endif()
if(HADES_DOTNET_ROOT_CONFIG)
  file(TO_CMAKE_PATH "${HADES_DOTNET_ROOT_CONFIG}" HADES_DOTNET_ROOT_CONFIG)
endif()

if(HADES_DOTNET_EXECUTABLE_CONFIG)
  message(STATUS "Using dotnet SDK at: ${HADES_DOTNET_EXECUTABLE_CONFIG}")
else()
  message(STATUS "dotnet SDK not found during configure; scripting falls back to runtime PATH/DOTNET_ROOT lookup")
endif()

configure_file(
  "${CMAKE_SOURCE_DIR}/cmake/dotnet_config.hpp.in"
  "${CMAKE_BINARY_DIR}/generated/dotnet_config.hpp"
  @ONLY)

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

# ---------------------------------------------------------------------------
# HadesEngine static library (engine core, shared by editor and runtime)
# ---------------------------------------------------------------------------
add_library(HadesEngine STATIC ${ENGINE_SOURCE_FILES} ${ENGINE_HEADER_FILES})
target_include_directories(HadesEngine PUBLIC "${SRC_DIR}" "${CMAKE_BINARY_DIR}/generated")
if(HADES_FRAME_METRICS)
  target_compile_definitions(HadesEngine PUBLIC HADES_ENABLE_FRAME_METRICS)
endif()
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

# ---------------------------------------------------------------------------
# HadesGameEngine editor executable
# ---------------------------------------------------------------------------
add_executable(${PROJECT_NAME} ${EDITOR_SOURCE_FILES} ${EDITOR_HEADER_FILES})
target_include_directories(${PROJECT_NAME} PRIVATE "${CMAKE_BINARY_DIR}/generated")
target_link_libraries(
  ${PROJECT_NAME}
  PRIVATE HadesEngine
          CLI11::CLI11
          hades_imgui_textedit)
hades_stage_runtime_dependency(${PROJECT_NAME} assimp)

add_custom_command(
  TARGET ${PROJECT_NAME}
  POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${PROJECT_NAME}>/assets"
  COMMAND
    ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_SOURCE_DIR}/assets/logo.bmp"
    "$<TARGET_FILE_DIR:${PROJECT_NAME}>/assets/logo.bmp")

if(TARGET SDL2::SDL2main)
  target_link_libraries(${PROJECT_NAME} PRIVATE SDL2::SDL2main)
endif()

# ---------------------------------------------------------------------------
# HadesRuntime standalone game executable
# ---------------------------------------------------------------------------
add_executable(HadesRuntime ${RUNTIME_SOURCE_FILES} ${RUNTIME_HEADER_FILES})
target_include_directories(HadesRuntime PRIVATE "${CMAKE_BINARY_DIR}/generated")
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

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
# Tests reference a few editor-specific sources (workspace manager, settings,
# script analysis, plugin registry) in addition to the engine library.
set(TEST_EDITOR_SUPPORT_SOURCES
  src/editor/editor_settings.cpp
  src/editor/plugins/editor_plugin.cpp
  src/editor/script_analysis.cpp
  src/editor/script_document.cpp
  src/editor/workspace_manager.cpp
  src/editor/workspace_file_operations.cpp)

add_executable(hades_tests ${TEST_SOURCE_FILES} ${TEST_EDITOR_SUPPORT_SOURCES})
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
