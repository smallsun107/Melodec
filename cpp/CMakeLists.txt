cmake_minimum_required(VERSION 3.16)
project(ReNcm LANGUAGES C CXX ASM)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

include(FetchContent)

# Keep the executables in the top-level build dir (e.g. build/rencm-gui)
# instead of build/gui/.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

# CMake >= 3.31 rejects dependencies that declare cmake_minimum_required < 3.5.
# GLFW 3.4 still declares 3.1, so allow the old minimum.
set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "Allow older dependencies" FORCE)
set(ENV{CMAKE_POLICY_VERSION_MINIMUM} 3.5)

# ---------------------------------------------------------------- targets
# rencm      : the NCM decoder library (no third-party dependencies)
# rencm-gui  : the ImGui front-end (GLFW + Dear ImGui + bundled fonts)
# rencm_tests: CTest driver for the decoder (see tests/)
#
# RENCM_BUILD_GUI=OFF builds only the dependency-free library + tests, which is
# what CI uses so it does not have to fetch GLFW / Dear ImGui.
option(RENCM_BUILD_GUI "Build the ImGui front-end (fetches GLFW/ImGui)" ON)
option(RENCM_BUILD_TESTS "Build the decoder test driver" ON)

add_subdirectory(rencm)
if(RENCM_BUILD_GUI)
  add_subdirectory(gui)
endif()
if(RENCM_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
