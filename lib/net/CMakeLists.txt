# lib/net/CMakeLists.txt

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

add_library(tb_net STATIC)
add_library(tb::net ALIAS tb_net)

set_target_properties(tb_net PROPERTIES EXPORT_NAME net)

set(NET_PUBLIC_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/include/tb/net/http/chunked_encoding.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/tb/net/http/cookie.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/tb/net/http/cookie_jar.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/tb/net/http/encoding.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/tb/net/http/error.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/tb/net/http/http_client.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/tb/net/http/mime.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/tb/net/http/redirect_policy.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/tb/net/http/url.hpp)

set(NET_SOURCES
    src/tb/net/http/http_client.cpp
    src/tb/net/http/cookie.cpp
    src/tb/net/http/cookie_jar.cpp
    src/tb/net/http/gzip_decoder.cpp
    src/tb/net/http/br_decoder.cpp
    src/tb/net/http/mime.cpp)

target_sources(
  tb_net
  PRIVATE ${NET_SOURCES}
  PUBLIC FILE_SET
         HEADERS
         BASE_DIRS
         ${CMAKE_CURRENT_SOURCE_DIR}/include
         FILES
         ${NET_PUBLIC_HEADERS})

target_include_directories(tb_net PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
                                         $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)

target_compile_features(tb_net PUBLIC cxx_std_23)

find_package(OpenSSL REQUIRED)
find_package(Microsoft.GSL CONFIG REQUIRED)
find_package(glaze CONFIG REQUIRED)
find_package(ZLIB REQUIRED)

find_package(boost_asio CONFIG REQUIRED)
find_package(boost_beast CONFIG REQUIRED)

target_link_libraries(
  tb_net
  PUBLIC tb_utils
         Boost::asio
         Boost::beast
         OpenSSL::SSL
         OpenSSL::Crypto
         glaze::glaze
         Microsoft.GSL::GSL
         ZLIB::ZLIB)

find_package(Brotli QUIET)
if(Brotli_FOUND)
  target_link_libraries(tb_net PUBLIC Brotli::brotlidec)
else()
  find_package(unofficial-brotli CONFIG REQUIRED)
  if(TARGET unofficial::brotli::brotlidec AND NOT TARGET Brotli::brotlidec)
    add_library(Brotli::brotlidec INTERFACE IMPORTED)
    set_property(TARGET Brotli::brotlidec PROPERTY INTERFACE_LINK_LIBRARIES unofficial::brotli::brotlidec)
  endif()
  target_link_libraries(tb_net PUBLIC Brotli::brotlidec)
endif()

if(WIN32)
  target_link_libraries(tb_net PUBLIC ws2_32)
endif()

install(
  TARGETS tb_net
  EXPORT tb_netTargets
  FILE_SET HEADERS
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

install(
  EXPORT tb_netTargets
  NAMESPACE tb::
  FILE tb_netTargets.cmake
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/tb_net)

write_basic_package_version_file(
  ${CMAKE_CURRENT_BINARY_DIR}/tb_netConfigVersion.cmake
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY SameMajorVersion)

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/tb_netConfig.cmake ${CMAKE_CURRENT_BINARY_DIR}/tb_netConfigVersion.cmake
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/tb_net)
