# cmake/CompilerWarnings.cmake

include_guard(GLOBAL)

if(NOT DEFINED WARNINGS_AS_ERRORS)
  option(WARNINGS_AS_ERRORS "Treat warnings as errors (Debug only)" ON)
endif()

function(project_set_warnings target)
  if(NOT TARGET "${target}")
    message(FATAL_ERROR "project_set_warnings: target '${target}' does not exist")
  endif()

  get_target_property(_type "${target}" TYPE)
  if(_type STREQUAL "INTERFACE_LIBRARY")
    message(TRACE "project_set_warnings: skipping INTERFACE target '${target}'")
    return()
  endif()

  set(_scope PRIVATE)

  if(CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
    target_compile_options(
      ${target}
      ${_scope}
      /W4
      /permissive-
      /Zc:__cplusplus
      /Zc:preprocessor
      /EHsc
      /bigobj
      /diagnostics:column
      $<$<AND:$<BOOL:${WARNINGS_AS_ERRORS}>,$<CONFIG:Debug>>:/WX>)
  else()
    set(WARNINGS_CXX
        -Wall
        -Wextra
        -Wpedantic
        -Wshadow
        -Wold-style-cast
        -Woverloaded-virtual
        -Wconversion
        -Wsign-conversion
        -Wnon-virtual-dtor
        -Wcast-align
        -Wnull-dereference
        -Wdouble-promotion
        -Wformat=2
        -Wimplicit-fallthrough)

    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
      list(
        APPEND
        WARNINGS_CXX
        -Wmisleading-indentation
        -Wduplicated-cond
        -Wduplicated-branches
        -Wlogical-op
        -Wuseless-cast
        -Wsuggest-override)
    elseif(CMAKE_CXX_COMPILER_ID MATCHES ".*Clang")
      list(APPEND WARNINGS_CXX -Winconsistent-missing-override)
    endif()

    target_compile_options(
      ${target}
      ${_scope}
      $<$<COMPILE_LANGUAGE:CXX>:${WARNINGS_CXX}>
      $<$<AND:$<BOOL:${WARNINGS_AS_ERRORS}>,$<CONFIG:Debug>>:-Werror>)
  endif()
endfunction()

function(project_mark_system_includes target)
  if(NOT TARGET "${target}")
    message(FATAL_ERROR "project_mark_system_includes: target '${target}' does not exist")
  endif()
  if(ARGC GREATER 1)
    target_include_directories(${target} SYSTEM PRIVATE ${ARGN})
  endif()
endfunction()
