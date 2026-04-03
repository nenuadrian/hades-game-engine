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

# Define the source directory.
set(SRC_DIR "${CMAKE_SOURCE_DIR}/src")

# Automatically find application source and header files.
file(GLOB_RECURSE APP_SOURCE_FILES CONFIGURE_DEPENDS "${SRC_DIR}/*.cpp")
file(GLOB_RECURSE TEST_SOURCE_FILES CONFIGURE_DEPENDS "${SRC_DIR}/tests/*.cpp")
list(REMOVE_ITEM APP_SOURCE_FILES ${TEST_SOURCE_FILES})
file(
  GLOB_RECURSE HEADER_FILES
  CONFIGURE_DEPENDS
  "${SRC_DIR}/*.h"
  "${SRC_DIR}/*.hpp")

set(SOURCES ${APP_SOURCE_FILES} ${HEADER_FILES})

option(HADES_FRAME_METRICS "Enable frame metrics profiling" OFF)

add_executable(${PROJECT_NAME} ${SOURCES})
target_include_directories(${PROJECT_NAME} PRIVATE "${CMAKE_BINARY_DIR}/generated")
if(HADES_FRAME_METRICS)
  target_compile_definitions(${PROJECT_NAME} PRIVATE HADES_ENABLE_FRAME_METRICS)
endif()
target_link_libraries(
  ${PROJECT_NAME}
  PRIVATE CLI11::CLI11
          assimp
          hades_imgui
          hades_imgui_textedit
          miniaudio
          nlohmann_json::nlohmann_json
          tinyobjloader
          Vulkan::Vulkan)
if(WIN32)
  target_link_libraries(${PROJECT_NAME} PRIVATE Ole32 uuid)
else()
  target_link_libraries(${PROJECT_NAME} PRIVATE dl)
endif()
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

set(TEST_SUPPORT_SOURCES
  src/editor/editor_settings.cpp
  src/editor/plugins/editor_plugin.cpp
  src/editor/script_analysis.cpp
  src/editor/script_document.cpp
  src/editor/workspace_manager.cpp
  src/editor/workspace_file_operations.cpp
  src/engine/assets/model_importer.cpp
  src/engine/components/position_component_3d.cpp
  src/engine/components/transform_hierarchy_component.cpp
  src/engine/core/ecs/entity_factory.cpp
  src/engine/core/ecs/entity_manager.cpp
  src/engine/core/ecs/scene_serializer.cpp
  src/engine/rendering/vector_text.cpp
  src/engine/runtime/clr_host.cpp
  src/engine/runtime/script_runtime.cpp
  src/engine/runtime/subprocess.cpp)

add_executable(hades_tests ${TEST_SOURCE_FILES} ${TEST_SUPPORT_SOURCES})
target_include_directories(hades_tests PRIVATE "${CMAKE_BINARY_DIR}/generated")
target_link_libraries(hades_tests PRIVATE assimp gtest_main nlohmann_json::nlohmann_json)
if(NOT WIN32)
  target_link_libraries(hades_tests PRIVATE dl)
endif()
hades_stage_runtime_dependency(hades_tests assimp)

enable_testing()
include(GoogleTest)
if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.18)
  gtest_discover_tests(hades_tests DISCOVERY_MODE PRE_TEST)
else()
  gtest_discover_tests(hades_tests)
endif()
