# app/CMakeLists.txt - TwitchBotApp

add_executable(TwitchBotApp)

add_library(app_generated INTERFACE)
target_include_directories(app_generated INTERFACE
  $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/generated>
)

set(APP_INC ${CMAKE_CURRENT_SOURCE_DIR}/include)
set(APP_SRC ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_sources(TwitchBotApp
  PRIVATE
    ${APP_SRC}/main.cpp
    ${APP_SRC}/integrations.cpp
    ${APP_SRC}/app_channel_store.cpp
    ${APP_SRC}/register_integrations.cpp
    ${APP_SRC}/control_commands.cpp       
    ${APP_SRC}/channel_store.cpp
  PUBLIC
    FILE_SET HEADERS
    BASE_DIRS
      ${APP_INC}
    FILES
      ${APP_INC}/app/integrations.hpp
      ${APP_INC}/app/app_channel_store.hpp
      ${APP_INC}/app/register_integrations.hpp
      ${APP_INC}/app/control_commands.hpp   
      ${APP_INC}/app/channel_store.hpp
)

target_include_directories(TwitchBotApp
  PRIVATE
    ${APP_INC}
)

target_link_libraries(TwitchBotApp
  PRIVATE
    tb::twitch_core
    app_generated
)

find_package(glaze CONFIG REQUIRED)
find_package(tomlplusplus CONFIG REQUIRED)
target_link_libraries(TwitchBotApp PRIVATE glaze::glaze tomlplusplus::tomlplusplus)

target_compile_features(TwitchBotApp PRIVATE cxx_std_23)

if(MSVC)
  set_property(TARGET TwitchBotApp PROPERTY VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
endif()
