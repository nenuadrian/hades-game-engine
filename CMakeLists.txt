cmake_minimum_required(VERSION 3.16)
project(HadesGameEngine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(cmake/Dependencies.cmake)

find_package(Vulkan REQUIRED)
hades_configure_dependencies()

# Define the source directory.
set(SRC_DIR "${CMAKE_SOURCE_DIR}/src")

# Automatically find application source and header files.
file(GLOB_RECURSE APP_SOURCE_FILES CONFIGURE_DEPENDS "${SRC_DIR}/*.cpp")
list(REMOVE_ITEM APP_SOURCE_FILES "${CMAKE_SOURCE_DIR}/src/tests/test.cpp")
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
  src/engine/assets/model_importer.cpp
  src/engine/components/position_component_3d.cpp
  src/engine/components/transform_hierarchy_component.cpp
  src/engine/core/ecs/entity_factory.cpp
  src/engine/core/ecs/entity_manager.cpp
  src/engine/runtime/script_runtime.cpp
  src/engine/runtime/subprocess.cpp)

add_executable(hades_tests src/tests/test.cpp ${TEST_SUPPORT_SOURCES})
target_link_libraries(hades_tests PRIVATE assimp gtest_main)

enable_testing()
include(GoogleTest)
gtest_discover_tests(hades_tests)
