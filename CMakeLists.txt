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

add_executable(${PROJECT_NAME} ${SOURCES})
target_link_libraries(
  ${PROJECT_NAME}
  PRIVATE CLI11::CLI11
          assimp
          hades_imgui
          miniaudio
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
  src/editor/script_document.cpp
  src/editor/workspace_manager.cpp
  src/editor/workspace_file_operations.cpp
  src/engine/assets/model_importer.cpp
  src/engine/components/position_component_3d.cpp
  src/engine/components/transform_hierarchy_component.cpp
  src/engine/core/ecs/entity_factory.cpp
  src/engine/core/ecs/entity_manager.cpp
  src/engine/rendering/vector_text.cpp
  src/engine/runtime/clr_host.cpp
  src/engine/runtime/script_runtime.cpp
  src/engine/runtime/subprocess.cpp)

add_executable(hades_tests ${TEST_SOURCE_FILES} ${TEST_SUPPORT_SOURCES})
target_link_libraries(hades_tests PRIVATE assimp gtest_main)
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
