cmake_minimum_required(VERSION 3.10)
project(ImGuiMetalApp)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Add ImGui files
set(IMGUI_DIR ${CMAKE_SOURCE_DIR}/imgui)

set(SOURCES
    src/main.mm
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/backends/imgui_impl_metal.mm
    ${IMGUI_DIR}/backends/imgui_impl_osx.mm
)

# Create the executable
add_executable(${PROJECT_NAME} ${SOURCES})

# Include directories
include_directories(
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
)

# Link the Metal, MetalKit, and GameController frameworks (macOS)
target_link_libraries(${PROJECT_NAME}
    "-framework Cocoa"
    "-framework Metal"
    "-framework QuartzCore"
    "-framework MetalKit"         # Link against MetalKit
    "-framework GameController"   # Link against GameController
)
