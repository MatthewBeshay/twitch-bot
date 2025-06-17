# CMakeLists.txt (project root)

cmake_minimum_required(VERSION 4.0)
project(TwitchBot
  VERSION     0.1.0
  DESCRIPTION "Twitch chat bot"
  LANGUAGES   CXX
)

# export compile_commands.json for clang-tidy, language servers, etc
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(ENABLE_CLANG_TIDY "Run clang-tidy as part of the build" OFF)
if (ENABLE_CLANG_TIDY)
  set(CMAKE_CXX_CLANG_TIDY
      "clang-tidy;-p=${CMAKE_BINARY_DIR};-fix;-fix-errors;-format-style=file"
      CACHE STRING "" FORCE)
endif()

# C++ standard
set(CMAKE_CXX_STANDARD        23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS      OFF)

# formatting helper target
add_custom_target(format
  COMMAND clang-format -i --style=file
          ${CMAKE_SOURCE_DIR}/lib/src/*.cpp
          ${CMAKE_SOURCE_DIR}/lib/include/*.hpp
          ${CMAKE_SOURCE_DIR}/lib/utils/*.hpp
          ${CMAKE_SOURCE_DIR}/app/*.cpp
          ${CMAKE_SOURCE_DIR}/app/*.hpp
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# subdirectories
add_subdirectory(lib)
add_subdirectory(app)

if (MSVC)
  set_property(
    DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PROPERTY   VS_STARTUP_PROJECT TwitchBotApp
  )
endif()