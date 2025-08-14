# app/CMakeLists.txt - TwitchBotApp

add_executable(TwitchBotApp)

add_library(app_generated INTERFACE)
target_include_directories(app_generated INTERFACE $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/generated>)

target_sources(
  TwitchBotApp
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp ${CMAKE_CURRENT_SOURCE_DIR}/integrations.cpp
  PUBLIC FILE_SET
         HEADERS
         BASE_DIRS
         ${CMAKE_CURRENT_SOURCE_DIR}
         FILES
         ${CMAKE_CURRENT_SOURCE_DIR}/integrations.hpp)

target_link_libraries(TwitchBotApp PRIVATE tb::twitch_core app_generated)

target_compile_features(TwitchBotApp PRIVATE cxx_std_23)

if(MSVC)
  set_property(TARGET TwitchBotApp PROPERTY VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
endif()
