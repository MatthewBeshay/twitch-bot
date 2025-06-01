# -------------------------------------------------------------------------------
# CMakeLists.txt (project root)
# -------------------------------------------------------------------------------

# If CMAKE_TOOLCHAIN_FILE is not already defined, use vcpkg's toolchain
if (NOT DEFINED CMAKE_TOOLCHAIN_FILE AND
    EXISTS "${CMAKE_SOURCE_DIR}/external/vcpkg/scripts/buildsystems/vcpkg.cmake")
  set(CMAKE_TOOLCHAIN_FILE
      "${CMAKE_SOURCE_DIR}/external/vcpkg/scripts/buildsystems/vcpkg.cmake"
      CACHE STRING "Path to vcpkg toolchain file" FORCE)
endif()

cmake_minimum_required(VERSION 4.0)
project(
  TwitchBot
  VERSION 0.1.0
  LANGUAGES CXX
)

# -------------------------------------------------------------------------------
# Enforce C++20, no compiler extensions
# -------------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD      20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS    OFF)

# -------------------------------------------------------------------------------
# Silence legacy FindBoost warning
# -------------------------------------------------------------------------------
cmake_policy(SET CMP0167 NEW)

# -------------------------------------------------------------------------------
# Add subdirectories: lib (core) and app (executable)
# -------------------------------------------------------------------------------
add_subdirectory(lib)
add_subdirectory(app)

# -------------------------------------------------------------------------------
# On Visual Studio, set TwitchBotApp as the default startup project
# -------------------------------------------------------------------------------
if (MSVC)
  set_property(
    DIRECTORY PROPERTY VS_STARTUP_PROJECT TwitchBotApp
  )
endif()
