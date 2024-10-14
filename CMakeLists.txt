cmake_minimum_required(VERSION 3.10)
project(HadesGameEngine)

set(CMAKE_CXX_STANDARD 17)

# Define the source directory
set(SRC_DIR "${CMAKE_SOURCE_DIR}/src/engine")

# Automatically find all .cpp, .h, and .hpp files in the src directory
file(GLOB_RECURSE SRC_FILES "${SRC_DIR}/*.cpp" "${SRC_DIR}/*.h"
     "${SRC_DIR}/*.hpp")

# Add source files
set(SOURCES ${SRC_FILES} ${CMAKE_SOURCE_DIR}/src/main.cpp)

# Create the executable
add_executable(${PROJECT_NAME} ${SOURCES})

add_subdirectory(lib/bgfx)
add_subdirectory(lib/imgui)
#add_subdirectory(lib/SDL2)

include_directories(${CMAKE_SOURCE_DIR}/lib/bgfx/include)
include_directories(${CMAKE_SOURCE_DIR}/lib/imgui)
include_directories(${CMAKE_SOURCE_DIR}/lib/imgui/backends)
include_directories(${CMAKE_SOURCE_DIR}/lib/CLI11/include)
include_directories(${CMAKE_SOURCE_DIR}/lib/SDL2/include)

if(WIN32)
  target_link_libraries(${PROJECT_NAME} SDL2-static SDL2main ImGui bgfx) # On Windows
else()
  # Link against OpenGL
  find_library(OpenGL_LIBRARY OpenGL REQUIRED)
  target_link_libraries(${PROJECT_NAME} ${OpenGL_LIBRARY})

  target_link_libraries(${PROJECT_NAME} SDL2 SDL2main ImGui bgfx) # On Linux/macOS

  if(APPLE)
    # Link ImGui and SDL dependencies
    target_link_libraries(${PROJECT_NAME} "-framework Cocoa" "-framework IOKit"
                          "-framework CoreVideo")
  endif()

endif()

# Add GoogleTest subdirectory
add_subdirectory(lib/googletest)

# Add your test executable
add_executable(hades_tests src/tests/test.cpp)

if(WIN32)
  # Link against static gtest on Windows
  target_link_libraries(hades_tests gtest gtest_main)
  target_compile_definitions(hades_tests
                             PRIVATE GTEST_LINKED_AS_SHARED_LIBRARY=0)

else()
  # Link against gtest dynamically on Linux/macOS
  target_link_libraries(hades_tests gtest gtest_main)
endif()

if(MSVC)
  set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /MD")
  set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /MDd")
  set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /NODEFAULTLIB:LIBCMT")
endif()

enable_testing()

# Discover and register tests
include(GoogleTest)
gtest_discover_tests(hades_tests)
