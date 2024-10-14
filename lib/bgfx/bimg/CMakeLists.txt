cmake_minimum_required(VERSION 3.10)

# Set the project name
project(bimgLibrary)

# Set the C++ standard to C++17 (or the version required by bimg)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Add include directories (for bimg and 3rdparty libraries)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/bx/include)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/3rdparty)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/astc-encoder/include)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/tinyexr/deps/miniz)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/iqa/include)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/nvtt)

# Set the source files from the src directory
file(GLOB_RECURSE BIMG_3RDPARTY_HEADERS
     ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/*.h
     ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/*.inl)

file(GLOB_RECURSE BIMG_SRC ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
     ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/*.cpp)


# Create a static library for bimg
add_library(bimg STATIC ${BIMG_SRC} ${BIMG_3RDPARTY_HEADERS})

add_subdirectory(bx)

# Specify dependencies for bimg
add_dependencies(bimg bx)

# Define BX_CONFIG_DEBUG based on build type
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  target_compile_definitions(bimg PRIVATE BX_CONFIG_DEBUG=1)
else()
  target_compile_definitions(bimg PRIVATE BX_CONFIG_DEBUG=0)
endif()

# Add the third-party libraries (if they need to be compiled separately) You can
# link directly to prebuilt libraries or compile from source if needed. Example
# for linking prebuilt third-party libraries (replace with actual paths):
# target_link_libraries(bimg PRIVATE
# ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/libetc2comp.a) target_link_libraries(bimg
# PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/libiqa.a)
# target_link_libraries(bimg PRIVATE
# ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/libsquish.a) target_link_libraries(bimg
# PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/libnvtt.a)

# Optionally, add definitions if needed (for platform-specific code)
if(WIN32)
  target_compile_definitions(bimg PRIVATE BX_PLATFORM_WINDOWS)
elseif(APPLE)
  target_compile_definitions(bimg PRIVATE BX_PLATFORM_OSX)
elseif(UNIX)
  target_compile_definitions(bimg PRIVATE BX_PLATFORM_LINUX)
endif()

# Optionally add compiler flags for optimizations, etc.
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  target_compile_options(bimg PRIVATE -g)
else()
  target_compile_options(bimg PRIVATE -O3)
endif()

# Link with external libraries if needed (example linking with OpenGL, Threads,
# etc.)
if(UNIX AND NOT APPLE)
  find_package(Threads REQUIRED)
  target_link_libraries(bimg PRIVATE Threads::Threads)
endif()
