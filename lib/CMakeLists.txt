# ------------------------------------------------------------------------------
# lib/CMakeLists.txt - the core library (TwitchBotCore)
# ------------------------------------------------------------------------------

# -- Find third-party packages (via vcpkg manifest mode) -----------------------
find_package(Boost        REQUIRED COMPONENTS asio beast)
find_package(OpenSSL      REQUIRED)
find_package(tomlplusplus CONFIG REQUIRED)
find_package(glaze        CONFIG REQUIRED)

# -- Gather all public headers (for IDE grouping only) -------------------------
file(GLOB_RECURSE TB_HEADERS
  CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/include/*.hpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/include/*.h"
)

# -- Build the static library from your .cpp source files ----------------------
add_library(TwitchBotCore
  src/channel_store.cpp
  src/command_dispatcher.cpp
  src/config.cpp
  src/helix_client.cpp
  src/http_client.cpp
  src/irc_client.cpp
  src/message_parser.cpp
  src/twitch_bot.cpp
)

# -- On Windows, set _WIN32_WINNT for minimum Windows version ------------------
if (WIN32)
  # Target Windows 10 (_WIN32_WINNT=0x0A00)
  target_compile_definitions(TwitchBotCore
    PUBLIC
      _WIN32_WINNT=0x0A00
  )
endif()

# -- Tell IDEs that these header files belong to TwitchBotCore -> "Header Files"
target_sources(TwitchBotCore
  PUBLIC
    ${TB_HEADERS}
)

source_group(
  TREE   "${CMAKE_CURRENT_SOURCE_DIR}/include"
  PREFIX "Header Files"
  FILES  ${TB_HEADERS}
)

# -- Public include directories for consumers of TwitchBotCore -----------------
#    1) lib/include/twitch_bot
#    2) lib/include
#    3) build/generated (for config_path.hpp)
target_include_directories(TwitchBotCore
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include/twitch_bot>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/generated>
    $<INSTALL_INTERFACE:include>
)

# -- Enforce C++20 on this target and any target that links to it --------------
target_compile_features(TwitchBotCore PUBLIC cxx_std_20)

# -- Warning flags -------------------------------------------------------------
if (MSVC)
  target_compile_options(TwitchBotCore PRIVATE /W4 /bigobj)
else()
  target_compile_options(TwitchBotCore PRIVATE -Wall -Wextra -pedantic)
endif()

# -- Third-party linkage -------------------------------------------------------
target_link_libraries(TwitchBotCore
  PUBLIC
    Boost::asio
    Boost::beast
    OpenSSL::SSL
    tomlplusplus::tomlplusplus
    glaze::glaze
)

# -- Create an alias so downstream code can just do `TwitchBot::Core` -----------
add_library(TwitchBot::Core ALIAS TwitchBotCore)
