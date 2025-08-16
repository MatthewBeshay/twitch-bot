# lib/net/CMakeLists.txt

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

add_library(tb_net STATIC)

set(NET_PUBLIC_HEADERS
  ${CMAKE_CURRENT_SOURCE_DIR}/include/tb/net/http/chunked_encoding.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/tb/net/http/cookie.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/tb/net/http/cookie_jar.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/tb/net/http/encoding.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/tb/net/http/error.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/tb/net/http/http_client.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/tb/net/http/mime.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/tb/net/http/redirect_policy.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/tb/net/http/url.hpp
)

set(NET_SOURCES
  src/tb/net/http/http_client.cpp
  src/tb/net/http/cookie.cpp
  src/tb/net/http/cookie_jar.cpp
  src/tb/net/http/gzip_decoder.cpp
  src/tb/net/http/mime.cpp
)

target_sources(tb_net
  PRIVATE
    ${NET_SOURCES}
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

find_package(ZLIB REQUIRED)

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
    ZLIB::ZLIB
)

# optional: brotli (br decoder)
option(TB_NET_WITH_BROTLI "Enable Brotli (br) response decoding when Brotli is available" ON)
if (TB_NET_WITH_BROTLI)
  # Try common target names produced by various FindBrotli packages
  find_package(Brotli QUIET)
  if (Brotli_FOUND)
    if (TARGET Brotli::dec)
      set(_TB_BROTLI_DEC_TARGET Brotli::dec)
    elseif (TARGET Brotli::brotlidec)
      set(_TB_BROTLI_DEC_TARGET Brotli::brotlidec)
    elseif (TARGET brotli::brotlidec)
      set(_TB_BROTLI_DEC_TARGET brotli::brotlidec)
    endif()
  endif()

  if (_TB_BROTLI_DEC_TARGET)
    target_sources(tb_net PRIVATE src/tb/net/http/br_decoder.cpp)
    target_link_libraries(tb_net PUBLIC ${_TB_BROTLI_DEC_TARGET})
    target_compile_definitions(tb_net PUBLIC USE_BROTLI=1)
  endif()
endif()

if (WIN32)
  target_link_libraries(tb_net PUBLIC ws2_32)
endif()

add_library(tb::net ALIAS tb_net)

# ---- install & package ----
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
