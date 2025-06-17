# lib/CMakeLists.txt – TwitchBotCore

add_library(TwitchBotCore
  src/channel_store.cpp
  src/command_dispatcher.cpp
  src/config.cpp
  src/faceit_client.cpp
  src/helix_client.cpp
  src/http_client.cpp
  src/irc_client.cpp
  src/record_service.cpp
  src/twitch_bot.cpp
)

file(GLOB_RECURSE TB_HEADERS
  CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/include/*.hpp"
)

target_sources(TwitchBotCore
  PRIVATE ${TB_HEADERS}
)

source_group(
  TREE   "${CMAKE_CURRENT_SOURCE_DIR}/include"
  PREFIX "Header Files"
  FILES  ${TB_HEADERS}
)

target_include_directories(TwitchBotCore
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include/twitch_bot>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_compile_features(TwitchBotCore PUBLIC cxx_std_23)

# compiler-specific warnings and options
target_compile_options(TwitchBotCore PRIVATE
  $<$<CXX_COMPILER_ID:MSVC>:
    /W4
    /bigobj
    /Zc:lambda
  >
  $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:
    -Wall
    -Wextra
    -pedantic
  >
)

if (WIN32)
  target_compile_definitions(TwitchBotCore
    PUBLIC _WIN32_WINNT=0x0A00
  )
endif()

find_package(Boost         REQUIRED COMPONENTS asio beast)
find_package(OpenSSL       REQUIRED)
find_package(tomlplusplus  CONFIG REQUIRED)
find_package(glaze         CONFIG REQUIRED)
find_package(Microsoft.GSL CONFIG REQUIRED)

target_link_libraries(TwitchBotCore
  PUBLIC
    Boost::asio
    Boost::beast
    OpenSSL::SSL
    tomlplusplus::tomlplusplus
    glaze::glaze
    Microsoft.GSL::GSL
)

add_library(TwitchBot::Core ALIAS TwitchBotCore)
