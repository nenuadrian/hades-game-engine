cmake_minimum_required(VERSION 3.10)
project(HadesGameEngine)

set(CMAKE_CXX_STANDARD 17)

# Add source files
set(SOURCES
    src/main.cpp
    ${CMAKE_SOURCE_DIR}/lib/imgui/imgui.cpp
    ${CMAKE_SOURCE_DIR}/lib/imgui/imgui_draw.cpp
    ${CMAKE_SOURCE_DIR}/lib/imgui/imgui_widgets.cpp
    ${CMAKE_SOURCE_DIR}/lib/imgui/imgui_tables.cpp
    ${CMAKE_SOURCE_DIR}/lib/imgui/backends/imgui_impl_sdl2.cpp
    ${CMAKE_SOURCE_DIR}/lib/imgui/backends/imgui_impl_opengl3.cpp)

# Create the executable
add_executable(${PROJECT_NAME} ${SOURCES})

add_subdirectory(lib/SDL2)

if(WIN32)
  target_link_libraries(${PROJECT_NAME} SDL2-static SDL2main) # On Windows
else()
  # Link against OpenGL
  find_library(OpenGL_LIBRARY OpenGL REQUIRED)
  target_link_libraries(${PROJECT_NAME} ${OpenGL_LIBRARY})

  target_link_libraries(${PROJECT_NAME} SDL2 SDL2main) # On Linux/macOS

  if(APPLE)
    # Link ImGui and SDL dependencies
    target_link_libraries(${PROJECT_NAME} "-framework Cocoa" "-framework IOKit"
                          "-framework CoreVideo")
  endif()

endif()

target_include_directories(${PROJECT_NAME} PRIVATE lib/SDL2/include)

# Include ImGui directories
include_directories(${CMAKE_SOURCE_DIR}/lib/imgui)
include_directories(${CMAKE_SOURCE_DIR}/lib/imgui/backends)

include_directories(${CMAKE_SOURCE_DIR}/lib/CLI11/include)

# Add GoogleTest subdirectory
add_subdirectory(lib/googletest)

# Add your test executable
add_executable(hades_tests src/tests/test.cpp)

if(WIN32)
  if(MSVC)
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /MD")
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /MDd")
  endif()
  # Link against static gtest on Windows
  target_link_libraries(hades_tests gtest gtest_main)
  target_compile_definitions(hades_tests
                             PRIVATE GTEST_LINKED_AS_SHARED_LIBRARY=0)

else()
  # Link against gtest dynamically on Linux/macOS
  target_link_libraries(hades_tests gtest gtest_main)
endif()

enable_testing()

# Discover and register tests
include(GoogleTest)
gtest_discover_tests(hades_tests)
