# lib/utils/CMakeLists.txt
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

add_library(tb_utils INTERFACE)
add_library(tb::utils ALIAS tb_utils)

set_target_properties(tb_utils PROPERTIES EXPORT_NAME utils)

set(UTILS_PUBLIC_HEADERS
  ${CMAKE_CURRENT_SOURCE_DIR}/include/tb/utils/attributes.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/tb/utils/timer.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/tb/utils/transparent_string_hash.hpp
)

target_sources(tb_utils
  INTERFACE
    FILE_SET HEADERS
      BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/include
      FILES ${UTILS_PUBLIC_HEADERS}
)

target_include_directories(tb_utils
  INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_compile_features(tb_utils INTERFACE cxx_std_23)

find_package(Microsoft.GSL CONFIG REQUIRED)
target_link_libraries(tb_utils INTERFACE Microsoft.GSL::GSL)

target_compile_definitions(tb_utils INTERFACE
  $<$<BOOL:${WIN32}>:_WIN32_WINNT=0x0A00>
  $<$<BOOL:${WIN32}>:WIN32_LEAN_AND_MEAN>
  $<$<BOOL:${WIN32}>:NOMINMAX>
)

install(TARGETS tb_utils
  EXPORT tb_utilsTargets
  FILE_SET HEADERS DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(EXPORT tb_utilsTargets
  NAMESPACE tb::
  FILE tb_utilsTargets.cmake
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/tb_utils
)

write_basic_package_version_file(
  ${CMAKE_CURRENT_BINARY_DIR}/tb_utilsConfigVersion.cmake
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY SameMajorVersion
)

install(FILES
  ${CMAKE_CURRENT_BINARY_DIR}/tb_utilsConfig.cmake
  ${CMAKE_CURRENT_BINARY_DIR}/tb_utilsConfigVersion.cmake
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/tb_utils
)
