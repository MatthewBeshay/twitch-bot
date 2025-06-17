# CMakeLists.txt (project root)

cmake_minimum_required(VERSION 4.0)
project(TwitchBot
  VERSION     0.1.0
  DESCRIPTION "Twitch chat bot"
  LANGUAGES   CXX
)

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

if(MSVC)
  add_compile_options(/Zc:preprocessor)
  set_property(
    DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PROPERTY   VS_STARTUP_PROJECT TwitchBotApp
  )
endif()