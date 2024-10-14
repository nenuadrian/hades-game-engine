cmake_minimum_required(VERSION 3.10)

# Set the project name
project(bgfxProject)

# Set the C++ standard to C++17 (required by bgfx)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Add include directories (for bgfx and third-party libraries)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/3rdparty)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/ect2comp)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/dds-ktx)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/bimg/include)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/bx/include)

# Add the source files from the src directory
file(GLOB BGFX_SRC ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)

if(APPLE)
    list(APPEND BGFX_SRC
        ${CMAKE_CURRENT_SOURCE_DIR}/src/renderer_mtl.mm
    )
endif()

# Create a static library for bgfx
add_library(bgfx STATIC ${BGFX_SRC})

# Define BX_CONFIG_DEBUG based on build type
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  target_compile_definitions(bgfx PRIVATE BX_CONFIG_DEBUG=1)
else()
  target_compile_definitions(bgfx PRIVATE BX_CONFIG_DEBUG=0)
endif()

# Optionally add BX and BIMG subdirectories, assuming they are part of the same
# project If they are in a different directory or as external projects, you
# should modify this
add_subdirectory(bimg)

# Link bgfx to bx and bimg
target_link_libraries(bgfx PRIVATE bx bimg)

# Optionally, set platform-specific definitions
if(WIN32)
  target_compile_definitions(bgfx PRIVATE BGFX_PLATFORM_WINDOWS)
elseif(APPLE)
  target_compile_definitions(bgfx PRIVATE BGFX_PLATFORM_OSX)
elseif(UNIX)
  target_compile_definitions(bgfx PRIVATE BGFX_PLATFORM_LINUX)
endif()

# Optionally, add compiler flags for optimizations, etc.
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  target_compile_options(bgfx PRIVATE -g)
else()
  target_compile_options(bgfx PRIVATE -O3)
endif()

# Link with external libraries if needed (OpenGL, Vulkan, etc.) Example for
# linking OpenGL:
find_package(OpenGL REQUIRED)
target_link_libraries(bgfx PRIVATE OpenGL::GL)

# Link against Metal framework on macOS
if(APPLE)
    target_link_libraries(bgfx PRIVATE "-framework Metal" "-framework QuartzCore")
endif()
