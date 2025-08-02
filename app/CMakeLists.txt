# app/CMakeLists.txt – TwitchBotApp

add_executable(TwitchBotApp
  main.cpp
)

if(MSVC)
  set_target_properties(TwitchBotApp PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
  )
endif()

target_link_libraries(TwitchBotApp
  PRIVATE TwitchBot::Core
)

target_include_directories(TwitchBotApp
  PRIVATE ${CMAKE_BINARY_DIR}/generated
)
