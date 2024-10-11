cmake_minimum_required(VERSION 3.10)
project(CrossPlatformSDLImGuiApp)

set(CMAKE_CXX_STANDARD 17)

# Add source files
set(SOURCES
    src/main.cpp
    ${CMAKE_SOURCE_DIR}/lib/imgui/imgui.cpp
    ${CMAKE_SOURCE_DIR}/lib/imgui/imgui_draw.cpp
    ${CMAKE_SOURCE_DIR}/lib/imgui/imgui_widgets.cpp
    ${CMAKE_SOURCE_DIR}/lib/imgui/imgui_tables.cpp
    ${CMAKE_SOURCE_DIR}/lib/imgui/backends/imgui_impl_sdl2.cpp
    ${CMAKE_SOURCE_DIR}/lib/imgui/backends/imgui_impl_opengl3.cpp
)

# Create the executable
add_executable(${PROJECT_NAME} ${SOURCES})

# Platform-specific configuration for SDL and ImGui
if(WIN32)
    # Windows-specific settings
    set(SDL2_PATH "${CMAKE_SOURCE_DIR}/lib/SDL2")

    # Include SDL2 directories for headers and link libraries
    include_directories("${SDL2_PATH}/include")
    target_link_libraries(${PROJECT_NAME} "${SDL2_PATH}/lib/x64/SDL2.lib" opengl32)

    # Copy the SDL2 DLL to the build output directory
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${SDL2_PATH}/lib/x64/SDL2.dll"
        $<TARGET_FILE_DIR:${PROJECT_NAME}>)

elseif(APPLE)
    # macOS-specific settings
    find_package(SDL2 REQUIRED)

    if(SDL2_FOUND)
        include_directories(${SDL2_INCLUDE_DIRS})
        target_link_libraries(${PROJECT_NAME} ${SDL2_LIBRARIES})
    else()
        message(FATAL_ERROR "SDL2 not found on macOS. Please install SDL2 using Homebrew.")
    endif()

    # Link against OpenGL
    find_library(OpenGL_LIBRARY OpenGL REQUIRED)
    target_link_libraries(${PROJECT_NAME} ${OpenGL_LIBRARY})
endif()

# Include ImGui directories
include_directories(${CMAKE_SOURCE_DIR}/lib/imgui)
include_directories(${CMAKE_SOURCE_DIR}/lib/imgui/backends)

include_directories(${CMAKE_SOURCE_DIR}/lib/CLI11/include)

# Link ImGui and SDL dependencies
target_link_libraries(${PROJECT_NAME}
    "-framework Cocoa"
    "-framework IOKit"
    "-framework CoreVideo"
)


# Add GoogleTest subdirectory
add_subdirectory(lib/googletest)


# Add your test executable
add_executable(my_project_tests src/tests/test.cpp)
target_link_libraries(my_project_tests gtest gtest_main)
enable_testing()

# Discover and register tests
include(GoogleTest)
gtest_discover_tests(my_project_tests)
