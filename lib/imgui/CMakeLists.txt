cmake_minimum_required(VERSION 3.10)

# Set the project name
project(ImGui)

# Set the C++ standard
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Add your ImGui source files here
set(IMGUI_SRC
    ${CMAKE_CURRENT_SOURCE_DIR}/imgui.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/imgui_draw.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/imgui_widgets.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/imgui_tables.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/backends/imgui_impl_sdl2.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp
    )

add_subdirectory(lib/SDL2)

# Create a static library
add_library(ImGui STATIC ${IMGUI_SRC})

# Include ImGui headers
target_include_directories(
  ImGui
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/backends)
include_directories(${CMAKE_SOURCE_DIR}/lib/SDL2/include)

find_package(Vulkan REQUIRED)


if(WIN32)
  target_link_libraries(${PROJECT_NAME} SDL2-static SDL2main  Vulkan::Vulkan) # On Windows
else()
  # Link against OpenGL
  find_library(OpenGL_LIBRARY OpenGL REQUIRED)
  target_link_libraries(ImGui ${OpenGL_LIBRARY})

  target_link_libraries(ImGui SDL2 SDL2main Vulkan::Vulkan) # On Linux/macOS

  if(APPLE)
    # Link ImGui and SDL dependencies
    target_link_libraries(ImGui "-framework Cocoa" "-framework IOKit"
                          "-framework CoreVideo" "-framework QuartzCore")
  endif()

endif()

