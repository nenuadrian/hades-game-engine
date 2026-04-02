include_guard(GLOBAL)

include(FetchContent)

option(
  HADES_USE_BUNDLED_DEPS
  "Prefer dependency sources already present under lib/ when available."
  ON)
option(
  HADES_ALLOW_DOWNLOADS
  "Allow CMake to download dependency sources when bundled copies are missing."
  ON)

set(HADES_CLI11_TAG "v2.4.2" CACHE STRING "CLI11 git tag or branch.")
set(HADES_GOOGLETEST_TAG "v1.15.2" CACHE STRING "GoogleTest git tag or branch.")
set(HADES_IMGUI_TAG "v1.91.4" CACHE STRING "Dear ImGui git tag or branch.")
# The vendored SDL snapshot is 2.31.0, which was a prerelease line.
# Use the nearest stable release tag for on-demand downloads.
set(HADES_SDL2_TAG "release-2.32.0" CACHE STRING "SDL2 git tag or branch.")
set(HADES_TINYOBJLOADER_TAG "v2.0.0rc13" CACHE STRING "tinyobjloader git tag or branch.")

function(hades_prefer_local_source fetch_name local_dir)
  string(TOUPPER "${fetch_name}" fetch_name_upper)
  set(fetchcontent_source_var "FETCHCONTENT_SOURCE_DIR_${fetch_name_upper}")

  if(HADES_USE_BUNDLED_DEPS AND EXISTS "${local_dir}")
    message(STATUS "Using bundled ${fetch_name} from ${local_dir}")
    set(${fetchcontent_source_var} "${local_dir}" PARENT_SCOPE)
    return()
  endif()

  if(NOT HADES_ALLOW_DOWNLOADS)
    message(
      FATAL_ERROR
        "Dependency '${fetch_name}' is not available at '${local_dir}' and "
        "HADES_ALLOW_DOWNLOADS=OFF. Either restore the local copy under lib/ "
        "or reconfigure with -DHADES_ALLOW_DOWNLOADS=ON.")
  endif()

  message(STATUS "Fetching ${fetch_name} into ${CMAKE_BINARY_DIR}/_deps")
endfunction()

function(hades_configure_dependencies)
  hades_prefer_local_source(cli11 "${CMAKE_SOURCE_DIR}/lib/CLI11")
  hades_prefer_local_source(googletest "${CMAKE_SOURCE_DIR}/lib/googletest")
  hades_prefer_local_source(imgui "${CMAKE_SOURCE_DIR}/lib/imgui")
  hades_prefer_local_source(sdl2 "${CMAKE_SOURCE_DIR}/lib/imgui/lib/SDL2")
  hades_prefer_local_source(tinyobjloader "${CMAKE_SOURCE_DIR}/lib/tinyobjloader")

  set(CLI11_BUILD_DOCS OFF CACHE BOOL "" FORCE)
  set(CLI11_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(CLI11_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(CLI11_INSTALL OFF CACHE BOOL "" FORCE)

  set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
  set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
  set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

  set(SDL_SHARED OFF CACHE BOOL "" FORCE)
  set(SDL_STATIC ON CACHE BOOL "" FORCE)
  set(SDL2_DISABLE_INSTALL ON CACHE BOOL "" FORCE)
  set(SDL_TEST OFF CACHE BOOL "" FORCE)
  set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
  set(SDL_TESTS OFF CACHE BOOL "" FORCE)

  set(TINYOBJLOADER_BUILD_TEST_LOADER OFF CACHE BOOL "" FORCE)
  set(TINYOBJLOADER_BUILD_OBJ_STICHER OFF CACHE BOOL "" FORCE)
  set(TINYOBJLOADER_WITH_PYTHON OFF CACHE BOOL "" FORCE)

  FetchContent_Declare(
    cli11
    GIT_REPOSITORY https://github.com/CLIUtils/CLI11.git
    GIT_TAG ${HADES_CLI11_TAG}
    GIT_SHALLOW TRUE)
  FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG ${HADES_GOOGLETEST_TAG}
    GIT_SHALLOW TRUE)
  FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG ${HADES_IMGUI_TAG}
    GIT_SHALLOW TRUE)
  FetchContent_Declare(
    sdl2
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG ${HADES_SDL2_TAG}
    GIT_SHALLOW TRUE)
  FetchContent_Declare(
    tinyobjloader
    GIT_REPOSITORY https://github.com/tinyobjloader/tinyobjloader.git
    GIT_TAG ${HADES_TINYOBJLOADER_TAG}
    GIT_SHALLOW TRUE)

  FetchContent_MakeAvailable(cli11 googletest sdl2 tinyobjloader)

  FetchContent_GetProperties(imgui)
  if(NOT imgui_POPULATED)
    cmake_policy(PUSH)
    if(POLICY CMP0169)
      cmake_policy(SET CMP0169 OLD)
    endif()
    FetchContent_Populate(imgui)
    cmake_policy(POP)
  endif()

  if(NOT TARGET hades_imgui)
    add_library(
      hades_imgui STATIC
      ${imgui_SOURCE_DIR}/imgui.cpp
      ${imgui_SOURCE_DIR}/imgui_draw.cpp
      ${imgui_SOURCE_DIR}/imgui_tables.cpp
      ${imgui_SOURCE_DIR}/imgui_widgets.cpp
      ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl2.cpp
      ${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp)

    target_include_directories(
      hades_imgui
      PUBLIC ${imgui_SOURCE_DIR}
             ${imgui_SOURCE_DIR}/backends)
    target_link_libraries(hades_imgui PUBLIC SDL2::SDL2 Vulkan::Vulkan)

    if(APPLE)
      target_link_libraries(hades_imgui PUBLIC "-framework QuartzCore")
    endif()
  endif()
endfunction()
