cmake_minimum_required(VERSION 3.16)
project(HadesGameEngine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(cmake/Dependencies.cmake)

find_package(Vulkan REQUIRED)
hades_configure_dependencies()

# Define the source directory.
set(SRC_DIR "${CMAKE_SOURCE_DIR}/src/engine")

# Automatically find all engine source and header files.
file(
  GLOB_RECURSE SRC_FILES
  CONFIGURE_DEPENDS
  "${SRC_DIR}/*.cpp"
  "${SRC_DIR}/*.h"
  "${SRC_DIR}/*.hpp")

set(SOURCES ${SRC_FILES} ${CMAKE_SOURCE_DIR}/src/main.cpp)

add_executable(${PROJECT_NAME} ${SOURCES})
target_link_libraries(
  ${PROJECT_NAME}
  PRIVATE CLI11::CLI11
          hades_imgui
          tinyobjloader
          Vulkan::Vulkan)

if(TARGET SDL2::SDL2main)
  target_link_libraries(${PROJECT_NAME} PRIVATE SDL2::SDL2main)
endif()

add_executable(hades_tests src/tests/test.cpp)
target_link_libraries(hades_tests PRIVATE gtest_main)

enable_testing()
include(GoogleTest)
gtest_discover_tests(hades_tests)
