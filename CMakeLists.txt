cmake_minimum_required(VERSION 4.0)
project(TwitchBot VERSION 0.1.0 DESCRIPTION "Twitch chat bot" LANGUAGES CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(MY_TIDY_CHECKS
  clang-diagnostic-*,
  bugprone-*,
  performance-*,
  modernize-*,
  readability-*
)

set(MY_TIDY_HEADER_FILTER "^(app|lib/src|lib/include/(twitch_bot|utils))/")

set(CMAKE_CXX_CLANG_TIDY
  clang-tidy;
  -checks=${MY_TIDY_CHECKS};
  -warnings-as-errors=modernize-use-nullptr;
  -header-filter=${MY_TIDY_HEADER_FILTER};
  -system-headers=false;
  -format-style=file
)

if (NOT DEFINED CMAKE_TOOLCHAIN_FILE
    AND EXISTS "${CMAKE_SOURCE_DIR}/external/vcpkg/scripts/buildsystems/vcpkg.cmake")
  set(CMAKE_TOOLCHAIN_FILE
      "${CMAKE_SOURCE_DIR}/external/vcpkg/scripts/buildsystems/vcpkg.cmake"
      CACHE STRING "" FORCE)
endif()

set(CMAKE_CXX_STANDARD      23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS    OFF)

if (MSVC)
  add_compile_options(/Zc:preprocessor)
endif()

add_custom_target(format
  COMMAND clang-format -i --style=file
          ${CMAKE_SOURCE_DIR}/lib/src/*.cpp
          ${CMAKE_SOURCE_DIR}/lib/include/*.hpp
          ${CMAKE_SOURCE_DIR}/lib/utils/*.hpp
          ${CMAKE_SOURCE_DIR}/app/*.cpp
          ${CMAKE_SOURCE_DIR}/app/*.hpp
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

add_subdirectory(lib)
add_subdirectory(app)

if (MSVC)
  set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT TwitchBotApp)
endif()
