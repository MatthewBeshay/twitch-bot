# lib/twitch_core/CMakeLists.txt

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

add_library(tb_twitch_core STATIC)

target_sources(
  tb_twitch_core
  PRIVATE
    src/chat_rate_limiter.cpp
    src/command_dispatcher.cpp
    src/config.cpp
    src/helix_client.cpp
    src/irc_client.cpp
    src/twitch_bot.cpp
  PUBLIC
    FILE_SET HEADERS
      BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/include
      FILES
        include/tb/parser/irc_message_parser.hpp
        include/tb/parser/irc_simd_scan.hpp
        include/tb/twitch/chat_rate_limiter.hpp
        include/tb/twitch/command_dispatcher.hpp
        include/tb/twitch/config.hpp
        include/tb/twitch/helix_client.hpp
        include/tb/twitch/irc_client.hpp
        include/tb/twitch/twitch_bot.hpp
)

target_include_directories(tb_twitch_core
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_compile_features(tb_twitch_core PUBLIC cxx_std_23)

find_package(tomlplusplus CONFIG REQUIRED)
find_package(glaze CONFIG REQUIRED)
find_package(Microsoft.GSL CONFIG REQUIRED)

target_link_libraries(
  tb_twitch_core
  PUBLIC
    tb_net
    tb_utils
    tomlplusplus::tomlplusplus
    glaze::glaze
    Microsoft.GSL::GSL
)

add_library(tb::twitch_core ALIAS tb_twitch_core)

install(
  TARGETS tb_twitch_core
  EXPORT tb_twitch_coreTargets
  FILE_SET HEADERS
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(
  EXPORT tb_twitch_coreTargets
  NAMESPACE tb::
  FILE tb_twitch_coreTargets.cmake
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/tb_twitch_core
)

configure_package_config_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/cmake/tb_twitch_coreConfig.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/tb_twitch_coreConfig.cmake
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/tb_twitch_core
)

install(FILES
  ${CMAKE_CURRENT_BINARY_DIR}/tb_twitch_coreConfig.cmake
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/tb_twitch_core
)
