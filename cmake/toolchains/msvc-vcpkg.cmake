# cmake/toolchains/msvc-vcpkg.cmake

# vcpkg integration
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE
   AND EXISTS "${CMAKE_SOURCE_DIR}/external/vcpkg/scripts/buildsystems/vcpkg.cmake")
  include("${CMAKE_SOURCE_DIR}/external/vcpkg/scripts/buildsystems/vcpkg.cmake")
endif()

# MSVC-only compiler flags
if(MSVC)
  add_compile_options(/Zc:preprocessor)
endif()
