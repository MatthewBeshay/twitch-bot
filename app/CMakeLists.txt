# app/CMakeLists.txt – TwitchBotApp

add_executable(TwitchBotApp
  main.cpp
)

target_link_libraries(TwitchBotApp
  PRIVATE TwitchBot::Core
)

target_include_directories(TwitchBotApp
  PRIVATE ${CMAKE_BINARY_DIR}/generated
)
