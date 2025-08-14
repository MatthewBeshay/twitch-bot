# CMakeLists.txt - project root

cmake_minimum_required(VERSION 4.00)

project(
  TwitchBot
  VERSION 0.1.0
  DESCRIPTION "Twitch chat bot"
  LANGUAGES CXX)

# --------------------------- Helper modules -----------------------------------

# Make sure our custom modules are visible
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

# Guard against in-source builds early
include(PreventInSourceBuilds)
project_prevent_in_source_builds()

# Per-target helpers
include(CompilerWarnings)
include(Sanitisers)
include(Hardening)
include(StaticAnalyzers)

# IPO availability check
include(CheckIPOSupported)
check_ipo_supported(RESULT _ipo_ok OUTPUT _ipo_msg)
if(NOT _ipo_ok)
  message(STATUS "IPO not enabled: ${_ipo_msg}")
endif()

# ------------------------------- Options --------------------------------------
option(USE_LLD "Use lld where supported (GNU-style linkers)" OFF)
option(WARNINGS_AS_ERRORS "Treat warnings as errors (Debug only via helper)" ON)
option(ENABLE_SANITISERS "Enable sanitiser flags in Debug when supported" ON)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(POLICY CMP0167)
  cmake_policy(SET CMP0167 NEW)
endif()

# ------------------------ Per-target link options helper ----------------------

function(project_set_link_options target)
  get_target_property(_type "${target}" TYPE)
  if(_type STREQUAL "INTERFACE_LIBRARY")
    return()
  endif()

  if(CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
    target_link_options(${target} PRIVATE $<$<CONFIG:Debug>:/DEBUG:FASTLINK>)
    target_link_options(
      ${target}
      PRIVATE
      $<$<CONFIG:Release,RelWithDebInfo>:/INCREMENTAL:NO>
      $<$<CONFIG:Release,RelWithDebInfo>:/OPT:REF>
      $<$<CONFIG:Release,RelWithDebInfo>:/OPT:ICF>
      $<$<CONFIG:Release,RelWithDebInfo>:/DEBUG>)
    if(_ipo_ok)
      set_property(TARGET ${target} PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
      set_property(TARGET ${target} PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO ON)
    endif()
  else()
    if(USE_LLD)
      target_link_options(${target} PRIVATE $<$<CONFIG:Release,RelWithDebInfo>:-fuse-ld=lld>)
    endif()
    target_link_options(
      ${target}
      PRIVATE
      $<$<CONFIG:Release,RelWithDebInfo>:-Wl,--as-needed>
      $<$<CONFIG:Release,RelWithDebInfo>:-Wl,--gc-sections>
      $<$<CONFIG:Release,RelWithDebInfo>:-Wl,-O2>
      $<$<AND:$<CONFIG:Release,RelWithDebInfo>,$<BOOL:${USE_LLD}>>:-Wl,--icf=all>)
    if(_ipo_ok)
      set_property(TARGET ${target} PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
      set_property(TARGET ${target} PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO ON)
    endif()
    # Pair gc-sections with function/data sections at compile time if not set elsewhere
    target_compile_options(${target} PRIVATE $<$<CONFIG:Release,RelWithDebInfo>:-ffunction-sections -fdata-sections>)
  endif()
endfunction()

# ------------------------------- Utilities ------------------------------------

file(
  GLOB_RECURSE
  FORMAT_CXX
  CONFIGURE_DEPENDS
  "${CMAKE_SOURCE_DIR}/lib/**/*.cpp"
  "${CMAKE_SOURCE_DIR}/lib/**/*.hpp"
  "${CMAKE_SOURCE_DIR}/app/**/*.cpp"
  "${CMAKE_SOURCE_DIR}/app/**/*.hpp")
add_custom_target(
  format
  COMMAND ${CMAKE_COMMAND} -E echo "Running clang-format over lib/ and app/"
  COMMAND clang-format -i --style=file ${FORMAT_CXX}
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  COMMENT "clang-format"
  VERBATIM)

file(
  GLOB_RECURSE
  _cmake_lists
  CONFIGURE_DEPENDS
  "CMakeLists.txt")
file(
  GLOB_RECURSE
  _cmake_modules
  CONFIGURE_DEPENDS
  "*.cmake")
set(_all_cmake_files ${_cmake_lists} ${_cmake_modules})

add_custom_target(
  format-cmake
  COMMAND ${CMAKE_COMMAND} -E echo "Formatting CMake files"
  COMMAND cmake-format -i ${_all_cmake_files}
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  VERBATIM)

# --------------------------------- Project ------------------------------------

add_subdirectory(lib)
add_subdirectory(app)

foreach(tgt IN ITEMS tb_net tb_twitch_core TwitchBotApp)
  if(TARGET ${tgt})
    project_set_warnings(${tgt})
    project_enable_sanitisers(${tgt})
    project_enable_hardening(${tgt})
    project_enable_clang_tidy(${tgt})
    project_enable_cppcheck(${tgt})
    project_enable_include_what_you_use(${tgt})
    project_set_link_options(${tgt})
  endif()
endforeach()

foreach(tgt IN ITEMS tb_net tb_twitch_core TwitchBotApp)
  if(MSVC AND TARGET ${tgt})
    set_property(TARGET ${tgt} PROPERTY VS_GLOBAL_BuildStlModules false)
  endif()
endforeach()

if(MSVC)
  set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT TwitchBotApp)
endif()

if(MSVC)
  set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT
      "$<$<CONFIG:Debug,RelWithDebInfo>:EditAndContinue>$<$<CONFIG:Release,MinSizeRel>:ProgramDatabase>")
endif()
