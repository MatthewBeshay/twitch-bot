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

# ─── Dependencies ─────────────────────────────────────────────────────────────
find_package(Boost 1.78 REQUIRED COMPONENTS system json)
find_package(OpenSSL REQUIRED)

# ─── Source File Organization ─────────────────────────────────────────────────
set(TWITCHBOT_HEADERS
  include/twitch_bot/config.hpp
  include/twitch_bot/faceit_client.hpp
  include/twitch_bot/http_client.hpp
  include/twitch_bot/irc_socket.hpp
  include/twitch_bot/twitch_bot.hpp
  include/utils/timer.hpp
)

set(TWITCHBOT_SOURCES
  src/config.cpp
  src/faceit_client.cpp
  src/http_client.cpp
  src/irc_socket.cpp
  src/twitch_bot.cpp
  src/main.cpp
  src/twitch_bot.cpp
)

# Visual Studio grouping
source_group("Header Files" FILES ${TWITCHBOT_HEADERS})
source_group("Source Files" FILES ${TWITCHBOT_SOURCES})

# ─── Library Target ───────────────────────────────────────────────────────────
add_library(TwitchBot ${TWITCHBOT_HEADERS} ${TWITCHBOT_SOURCES})

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

set_target_properties(TwitchBot PROPERTIES FOLDER "TwitchBot")

# ─── Executable Target ────────────────────────────────────────────────────────
add_executable(twitchbot_app src/main.cpp)

target_link_libraries(twitchbot_app PRIVATE TwitchBot)

set_target_properties(twitchbot_app PROPERTIES FOLDER "TwitchBot")
