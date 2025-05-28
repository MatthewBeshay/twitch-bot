cmake_minimum_required(VERSION 3.27)

# Silence the old FindBoost removal warning
cmake_policy(SET CMP0167 NEW)

project(TwitchBot VERSION 0.1.0 LANGUAGES CXX)

# ─── Auto-detect vcpkg toolchain ─────────────────────────────────────────────
if (NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  if (DEFINED ENV{VCPKG_ROOT}
      AND EXISTS "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake")
    set(CMAKE_TOOLCHAIN_FILE
        "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"
        CACHE FILEPATH "vcpkg toolchain (from VCPKG_ROOT)" FORCE)
  elseif (EXISTS "${CMAKE_SOURCE_DIR}/external/vcpkg/scripts/buildsystems/vcpkg.cmake")
    set(CMAKE_TOOLCHAIN_FILE
        "${CMAKE_SOURCE_DIR}/external/vcpkg/scripts/buildsystems/vcpkg.cmake"
        CACHE FILEPATH "vcpkg toolchain (from submodule)" FORCE)
  endif()
endif()
# ─────────────────────────────────────────────────────────────────────────────

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# find Boost (system + json) and OpenSSL
find_package(Boost 1.78 REQUIRED COMPONENTS system json)
find_package(OpenSSL REQUIRED)

# Library target
add_library(TwitchBot
  src/config.cpp
  src/faceit_client.cpp
  src/http_client.cpp
  src/irc_socket.cpp
  src/twitch_bot.cpp
)

target_include_directories(TwitchBot
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(TwitchBot
  PRIVATE
    Boost::system
    Boost::json
    OpenSSL::SSL
    OpenSSL::Crypto
)

# Executable
add_executable(twitchbot_app src/main.cpp)
target_link_libraries(twitchbot_app PRIVATE TwitchBot)
