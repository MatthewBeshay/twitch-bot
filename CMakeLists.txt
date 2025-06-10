# -------------------------------------------------------------------------------
# CMakeLists.txt (project root)
# -------------------------------------------------------------------------------
cmake_minimum_required(VERSION 4.0)
project(
  TwitchBot
  VERSION 0.1.0
  LANGUAGES CXX
)

#-------------------------------------------------------------------------------
# If vcpkg manifest detected, load it
#-------------------------------------------------------------------------------
if (NOT DEFINED CMAKE_TOOLCHAIN_FILE AND
    EXISTS "${CMAKE_SOURCE_DIR}/external/vcpkg/scripts/buildsystems/vcpkg.cmake")
  set(CMAKE_TOOLCHAIN_FILE
      "${CMAKE_SOURCE_DIR}/external/vcpkg/scripts/buildsystems/vcpkg.cmake"
      CACHE STRING "Path to vcpkg toolchain file" FORCE)
endif()

#-------------------------------------------------------------------------------
# Enforce C++23, no compiler extensions
#-------------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD      23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS    OFF)

#-------------------------------------------------------------------------------
# On MSVC, enable the standard-conformant preprocessor
#-------------------------------------------------------------------------------
if (MSVC)
  add_compile_options(/Zc:preprocessor)
endif()

#-------------------------------------------------------------------------------
# Subdirectories
#-------------------------------------------------------------------------------
add_subdirectory(lib)
add_subdirectory(app)

if (MSVC)
  set_property(
    DIRECTORY PROPERTY VS_STARTUP_PROJECT TwitchBotApp
  )
endif()