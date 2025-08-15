# lib/net/CMakeLists.txt

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

add_library(tb_net STATIC)

set(NET_PUBLIC_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/include/tb/net/http_client.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/tb/net/chunked_encoding.hpp
)

target_sources(tb_net
  PRIVATE
    src/tb/net/http_client.cpp
  PUBLIC
    FILE_SET HEADERS
      BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/include
      FILES ${NET_PUBLIC_HEADERS}
)

target_include_directories(tb_net
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_compile_features(tb_net PUBLIC cxx_std_23)

find_package(Boost CONFIG REQUIRED COMPONENTS asio beast)
find_package(OpenSSL REQUIRED)
find_package(Microsoft.GSL CONFIG REQUIRED)
find_package(glaze CONFIG REQUIRED)

target_link_libraries(
  tb_net
  PUBLIC
    tb_utils
    Boost::asio
    Boost::beast
    OpenSSL::SSL
    OpenSSL::Crypto
    glaze::glaze
    Microsoft.GSL::GSL
)

if(WIN32)
  target_link_libraries(tb_net PUBLIC ws2_32)
endif()

add_library(tb::net ALIAS tb_net)

install(
  TARGETS tb_net
  EXPORT tb_netTargets
  FILE_SET HEADERS DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
  INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
  ARCHIVE  DESTINATION ${CMAKE_INSTALL_LIBDIR}
  LIBRARY  DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME  DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(
  EXPORT tb_netTargets
  NAMESPACE tb::
  FILE tb_netTargets.cmake
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/tb_net
)

configure_package_config_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/cmake/tb_netConfig.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/tb_netConfig.cmake
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/tb_net
)

install(FILES
  ${CMAKE_CURRENT_BINARY_DIR}/tb_netConfig.cmake
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/tb_net
)
