cmake_minimum_required(VERSION 4.0)
project(TwitchBot VERSION 0.1.0 LANGUAGES CXX)

# -- Don't re-generate ZERO_CHECK
set(CMAKE_SUPPRESS_REGENERATION ON)

# -- C++20, no extensions
set(CMAKE_CXX_STANDARD         20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS       OFF)

# -- vcpkg manifest-mode toolchain
if (NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  if (DEFINED ENV{VCPKG_ROOT}
      AND EXISTS "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake")
    set(CMAKE_TOOLCHAIN_FILE
        "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"
        CACHE FILEPATH "" FORCE)
  elseif (EXISTS "${CMAKE_SOURCE_DIR}/external/vcpkg/scripts/buildsystems/vcpkg.cmake")
    set(CMAKE_TOOLCHAIN_FILE
        "${CMAKE_SOURCE_DIR}/external/vcpkg/scripts/buildsystems/vcpkg.cmake"
        CACHE FILEPATH "" FORCE)
  endif()
endif()

# -- Silence legacy FindBoost warning
cmake_policy(SET CMP0167 NEW)

# -- Pull in manifest-mode ports
find_package(Boost  REQUIRED COMPONENTS asio beast json)
find_package(OpenSSL REQUIRED)
find_package(tomlplusplus CONFIG REQUIRED)

# -- Sources & headers
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

# -- Executable target
add_executable(TwitchBot
  ${TB_SOURCES}
  ${TB_HEADERS}    # for IDE grouping
)

# -- IDE startup project
set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT TwitchBot)

# -- Public include interface
target_include_directories(TwitchBot
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>        # when installed
)

# -- Windows tweaks
if (WIN32)
  target_compile_definitions(TwitchBot PRIVATE _WIN32_WINNT=0x0A00)
endif()

# -- Compile settings
target_compile_features(TwitchBot PRIVATE cxx_std_20)

if (MSVC)
  target_compile_options(TwitchBot PRIVATE /bigobj /W4)
else()
  target_compile_options(TwitchBot PRIVATE -Wall -Wextra -pedantic)
endif()

# -- Link against vcpkg targets
target_link_libraries(TwitchBot
  PRIVATE
    Boost::asio
    Boost::beast
    Boost::json
    OpenSSL::SSL
    tomlplusplus::tomlplusplus
)

# -- Installation (optional)
install(TARGETS TwitchBot
  RUNTIME DESTINATION bin
)
install(DIRECTORY ${CMAKE_SOURCE_DIR}/include/ DESTINATION include)
