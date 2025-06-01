# ---------------------------------------------------------------
# app/CMakeLists.txt - the executable (TwitchBotApp)
# ---------------------------------------------------------------

add_executable(TwitchBotApp
  main.cpp
)

# Link against our core library
target_link_libraries(TwitchBotApp
  PRIVATE TwitchBot::Core
)

# Make the generated header (config_path.hpp) visible to this target
# (so that main.cpp can do #include "config_path.hpp" if needed)
target_include_directories(TwitchBotApp
  PRIVATE
    ${CMAKE_BINARY_DIR}/generated
)

# If you open the 'app' directory in Visual Studio, make this the startup project:
set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT TwitchBotApp)
