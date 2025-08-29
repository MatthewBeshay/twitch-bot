cmake_minimum_required(VERSION 3.20...4.1)
project(TwitchBot VERSION 0.1.0 LANGUAGES CXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

include(PreventInSourceBuilds)
project_prevent_in_source_builds()

include(CompilerWarnings)
include(Sanitisers)
include(Hardening)
include(StaticAnalyzers)

include(CheckIPOSupported)
check_ipo_supported(RESULT _ipo_ok OUTPUT _ipo_msg)
if(NOT _ipo_ok)
  message(STATUS "IPO not enabled: ${_ipo_msg}")
endif()

# Build configuration options
option(USE_LLD "Use lld where supported (GNU-style linkers)" OFF)
option(WARNINGS_AS_ERRORS "Treat warnings as errors (Debug only via helper)" ON)
option(ENABLE_SANITISERS "Enable sanitiser flags in Debug when supported" ON)
option(ENABLE_LTO "Enable link time optimisation when supported" ON)
option(ENABLE_INSTALL "Enable installation of targets" ON)
option(ENABLE_TESTING "Enable building tests" ON)
option(USE_LIBCXX "Use libc++ when available (Clang only)" OFF)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Hide symbols by default (non-MSVC) to keep ABI clean
if(NOT MSVC)
  set(CMAKE_CXX_VISIBILITY_PRESET hidden)
  set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)
endif()

# Large file support macros for POSIX platforms
if(NOT MSVC)
  add_compile_definitions(_FILE_OFFSET_BITS=64 _LARGEFILE64_SOURCE _LARGEFILE_SOURCE)
  if(CYGWIN)
    add_compile_definitions(_GNU_SOURCE=1)
  endif()
  if(QNXNTO)
    add_compile_definitions(_QNX_SOURCE)
  endif()
endif()

# Policy tweaks
if(POLICY CMP0167)
  cmake_policy(SET CMP0167 NEW)
endif()

# libc++ opt-in (Clang only)
if(USE_LIBCXX)
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    add_compile_options(-stdlib=libc++)
    set_property(GLOBAL PROPERTY USE_LIBCXX ON)
  else()
    message(FATAL_ERROR "USE_LIBCXX is only supported with Clang")
  endif()
endif()

# Threads once, then link per target
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

# Helper to set link options per target
function(project_set_link_options target)
  get_target_property(_type "${target}" TYPE)
  if(_type STREQUAL "INTERFACE_LIBRARY")
    return()
  endif()

  # If a target was sanitised, skip IPO and aggressive ICF for it.
  # Sanitisers set TB_SANITIZED TRUE on the target.
  get_property(_tb_sanitized TARGET ${target} PROPERTY TB_SANITIZED)

  if(CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
    target_link_options(${target} PRIVATE $<$<CONFIG:Debug>:/DEBUG:FASTLINK>)
    # Always keep PDBs in Release/RWDI for easier symbolisation
    target_link_options(${target} PRIVATE
      $<$<CONFIG:Release,RelWithDebInfo>:/INCREMENTAL:NO>
      $<$<CONFIG:Release,RelWithDebInfo>:/DEBUG>
    )
    if(NOT _tb_sanitized)
      target_link_options(${target} PRIVATE
        $<$<CONFIG:Release,RelWithDebInfo>:/OPT:REF>
        $<$<CONFIG:Release,RelWithDebInfo>:/OPT:ICF>
      )
    endif()
    if(_ipo_ok AND ENABLE_LTO AND NOT _tb_sanitized)
      set_property(TARGET ${target} PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
      set_property(TARGET ${target} PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO ON)
    endif()

  else()
    if(USE_LLD)
      include(CheckLinkerFlag)
      check_linker_flag(CXX "-fuse-ld=lld" HAVE_LLD_FLAG)
      if(HAVE_LLD_FLAG)
        target_link_options(${target} PRIVATE $<$<CONFIG:Release,RelWithDebInfo>:-fuse-ld=lld>)
      else()
        message(STATUS "USE_LLD requested but not supported by this toolchain")
      endif()
    endif()
    target_link_options(${target} PRIVATE
      $<$<CONFIG:Release,RelWithDebInfo>:-Wl,--as-needed>
      $<$<CONFIG:Release,RelWithDebInfo>:-Wl,--gc-sections>
      $<$<CONFIG:Release,RelWithDebInfo>:-Wl,-O2>
      # Only enable identical code folding when lld is used and the target is not sanitised
      $<$<AND:$<CONFIG:Release,RelWithDebInfo>,$<BOOL:${USE_LLD}>,$<NOT:$<BOOL:${_tb_sanitized}>>>:-Wl,--icf=all>
    )
    target_compile_options(${target} PRIVATE
      $<$<CONFIG:Release,RelWithDebInfo>:-ffunction-sections -fdata-sections>
    )
    # IPO only when requested and not sanitised
    if(_ipo_ok AND ENABLE_LTO AND NOT _tb_sanitized)
      set_property(TARGET ${target} PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
      set_property(TARGET ${target} PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO ON)
    endif()
  endif()

  # Link libc++ if globally requested
  get_property(_use_libcxx GLOBAL PROPERTY USE_LIBCXX)
  if(_use_libcxx AND NOT MSVC)
    target_link_libraries(${target} PRIVATE c++)
  endif()

  # Link pthreads explicitly
  target_link_libraries(${target} PRIVATE Threads::Threads)
endfunction()

# Formatting helpers
file(GLOB_RECURSE FORMAT_CXX CONFIGURE_DEPENDS
  "${CMAKE_SOURCE_DIR}/lib/**/*.cpp" "${CMAKE_SOURCE_DIR}/lib/**/*.hpp"
  "${CMAKE_SOURCE_DIR}/app/**/*.cpp" "${CMAKE_SOURCE_DIR}/app/**/*.hpp")
find_program(CLANG_FORMAT_EXE NAMES clang-format)
if(CLANG_FORMAT_EXE)
  add_custom_target(format
    COMMAND ${CMAKE_COMMAND} -E echo "clang-format lib/ and app/"
    COMMAND ${CLANG_FORMAT_EXE} -i --style=file ${FORMAT_CXX}
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    VERBATIM)
endif()

file(GLOB_RECURSE _cmake_lists CONFIGURE_DEPENDS "CMakeLists.txt")
file(GLOB_RECURSE _cmake_modules CONFIGURE_DEPENDS "*.cmake")
set(_all_cmake_files ${_cmake_lists} ${_cmake_modules})
find_program(CMAKE_FORMAT_EXE NAMES cmake-format)
if(CMAKE_FORMAT_EXE)
  add_custom_target(format-cmake
    COMMAND ${CMAKE_COMMAND} -E echo "Formatting CMake files"
    COMMAND ${CMAKE_FORMAT_EXE} -i ${_all_cmake_files}
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    VERBATIM)
endif()

# Project structure
add_subdirectory(lib)
add_subdirectory(app)

# Apply settings to all main targets
foreach(tgt IN ITEMS tb_utils tb_net tb_twitch_core TwitchBotApp)
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

# Install rules
if(ENABLE_INSTALL)
  install(TARGETS tb_utils tb_net tb_twitch_core TwitchBotApp
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
  )
  install(DIRECTORY lib/include/ DESTINATION include
          FILES_MATCHING PATTERN "*.hpp")
endif()

# MSVC IDE tweaks
if(MSVC)
  foreach(tgt IN ITEMS tb_utils tb_net tb_twitch_core TwitchBotApp)
    if(TARGET ${tgt})
      set_property(TARGET ${tgt} PROPERTY VS_GLOBAL_BuildStlModules false)
    endif()
  endforeach()
  set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT TwitchBotApp)
  set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT
      "$<$<CONFIG:Debug,RelWithDebInfo>:EditAndContinue>$<$<CONFIG:Release,MinSizeRel>:ProgramDatabase>")
endif()
