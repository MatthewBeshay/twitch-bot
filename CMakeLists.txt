cmake_minimum_required(VERSION 3.20)

# ─────────────────────────────────────────────────────────────────────
# Auto-detect vcpkg under external/vcpkg or via VCPKG_ROOT

if (NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  # 1) env var override
  if (DEFINED ENV{VCPKG_ROOT}
      AND EXISTS "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake")
    set(CMAKE_TOOLCHAIN_FILE
        "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"
        CACHE FILEPATH "vcpkg toolchain (from VCPKG_ROOT)" FORCE)

  # 2) repo-local submodule
  elseif (EXISTS "${CMAKE_SOURCE_DIR}/external/vcpkg/scripts/buildsystems/vcpkg.cmake")
    set(CMAKE_TOOLCHAIN_FILE
        "${CMAKE_SOURCE_DIR}/external/vcpkg/scripts/buildsystems/vcpkg.cmake"
        CACHE FILEPATH "vcpkg toolchain (from submodule)" FORCE)
  endif()
endif()

project(TwitchBot VERSION 0.1.0 LANGUAGES CXX)
# ─────────────────────────────────────────────────────────────────────

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Boost 1.78 REQUIRED COMPONENTS system json)
find_package(OpenSSL REQUIRED)

include_directories(${CMAKE_SOURCE_DIR}/include)

add_library(TwitchBot
  src/config.cpp
  src/faceit_client.cpp
  src/http_client.cpp
  src/irc_socket.cpp
  src/twitch_bot.cpp
)

target_link_libraries(TwitchBot
  PRIVATE Boost::system Boost::json OpenSSL::SSL OpenSSL::Crypto
)

add_executable(twitchbot_app src/main.cpp)
target_link_libraries(twitchbot_app PRIVATE TwitchBot)
