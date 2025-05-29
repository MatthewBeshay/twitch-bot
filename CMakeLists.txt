cmake_minimum_required(VERSION 4.00)
project(TwitchBot VERSION 0.1.0 LANGUAGES CXX)

# ── Don’t generate the ZERO_CHECK target (optional)
set(CMAKE_SUPPRESS_REGENERATION ON)

# ── C++20, no extensions
set(CMAKE_CXX_STANDARD     20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS   OFF)

# ── vcpkg manifest mode toolchain
if (NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  if (DEFINED ENV{VCPKG_ROOT}
      AND EXISTS "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake")
    set(CMAKE_TOOLCHAIN_FILE
        "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"
        CACHE FILEPATH "vcpkg toolchain file" FORCE
    )
  elseif (EXISTS "${CMAKE_SOURCE_DIR}/external/vcpkg/scripts/buildsystems/vcpkg.cmake")
    set(CMAKE_TOOLCHAIN_FILE
        "${CMAKE_SOURCE_DIR}/external/vcpkg/scripts/buildsystems/vcpkg.cmake"
        CACHE FILEPATH "vcpkg toolchain file" FORCE
    )
  endif()
endif()

# ── Silence legacy FindBoost warning
cmake_policy(SET CMP0167 NEW)

# ── Pull in your manifest-mode ports
find_package(boost_asio    CONFIG REQUIRED)
find_package(boost_beast   CONFIG REQUIRED)
find_package(boost_json    CONFIG REQUIRED)
find_package(OpenSSL       REQUIRED)

# ── Source & header lists
set(TB_SOURCES
  src/config.cpp
  src/faceit_client.cpp
  src/http_client.cpp
  src/irc_socket.cpp
  src/twitch_bot.cpp
  src/main.cpp
)

set(TB_HEADERS
  include/twitch_bot/config.hpp
  include/twitch_bot/faceit_client.hpp
  include/twitch_bot/http_client.hpp
  include/twitch_bot/irc_socket.hpp
  include/twitch_bot/twitch_bot.hpp
  include/utils/timer.hpp
)

# ── Single executable target
add_executable(TwitchBot
  ${TB_SOURCES}
  ${TB_HEADERS}    # for IDE grouping
)

# ── Tell VS to launch this project on F5
set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
             PROPERTY VS_STARTUP_PROJECT TwitchBot)

# ── Include paths so that #include "config.hpp" etc. just work
target_include_directories(TwitchBot PUBLIC
  $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
  $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include/twitch_bot>
  $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include/utils>
)

if (WIN32)
  target_compile_definitions(TwitchBot
    PRIVATE
      _WIN32_WINNT=0x0A00
  )
endif()

# ── Compile settings
target_compile_features(TwitchBot PUBLIC cxx_std_20)
if (MSVC)
  target_compile_options(TwitchBot PRIVATE /bigobj)
endif()

# ── Link against vcpkg-provided targets
target_link_libraries(TwitchBot PRIVATE
  Boost::asio
  Boost::beast
  Boost::json
  OpenSSL::SSL
)
