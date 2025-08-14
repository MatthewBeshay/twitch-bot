# StaticAnalyzers.cmake

include_guard(GLOBAL)

option(ENABLE_CLANG_TIDY "Enable clang-tidy for selected targets" OFF)
option(ENABLE_CPPCHECK "Enable cppcheck for selected targets" OFF)
option(ENABLE_IWYU "Enable include-what-you-use for selected targets" OFF)

# ---------- per-target helpers ----------

function(project_enable_clang_tidy target)
  if(NOT TARGET "${target}")
    message(FATAL_ERROR "project_enable_clang_tidy: target '${target}' does not exist")
  endif()
  if(NOT ENABLE_CLANG_TIDY)
    return()
  endif()

  # Skip INTERFACE libs
  get_target_property(_type "${target}" TYPE)
  if(_type STREQUAL "INTERFACE_LIBRARY")
    return()
  endif()

  find_program(CLANG_TIDY_EXECUTABLE NAMES clang-tidy)
  if(NOT CLANG_TIDY_EXECUTABLE)
    message(WARNING "clang-tidy requested but not found")
    return()
  endif()

  set(_opts
      "${CLANG_TIDY_EXECUTABLE}"
      -extra-arg=-Wno-unknown-warning-option
      -extra-arg=-Wno-ignored-optimization-argument
      -extra-arg=-Wno-unused-command-line-argument)

  if(CMAKE_CXX_STANDARD)
    if(CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
      list(APPEND _opts -extra-arg=/std:c++${CMAKE_CXX_STANDARD})
    else()
      list(APPEND _opts -extra-arg=-std=c++${CMAKE_CXX_STANDARD})
    endif()
  endif()

  if(WARNINGS_AS_ERRORS)
    list(APPEND _opts -warnings-as-errors=*)
  endif()

  set_property(TARGET "${target}" PROPERTY CXX_CLANG_TIDY "${_opts}")
endfunction()

function(project_enable_cppcheck target)
  if(NOT TARGET "${target}")
    message(FATAL_ERROR "project_enable_cppcheck: target '${target}' does not exist")
  endif()
  if(NOT ENABLE_CPPCHECK)
    return()
  endif()

  get_target_property(_type "${target}" TYPE)
  if(_type STREQUAL "INTERFACE_LIBRARY")
    return()
  endif()

  find_program(CPPCHECK_EXECUTABLE NAMES cppcheck)
  if(NOT CPPCHECK_EXECUTABLE)
    message(WARNING "cppcheck requested but not found")
    return()
  endif()

  if(CMAKE_GENERATOR MATCHES ".*Visual Studio.*")
    set(_tpl vs)
  else()
    set(_tpl gcc)
  endif()

  set(_opts
      "${CPPCHECK_EXECUTABLE}"
      --template=${_tpl}
      --enable=style,performance,warning,portability
      --inline-suppr
      --inconclusive
      --suppress=cppcheckError
      --suppress=internalAstError
      --suppress=unmatchedSuppression
      --suppress=passedByValue
      --suppress=syntaxError
      --suppress=preprocessorErrorDirective
      --suppress=knownConditionTrueFalse
      --suppress=*:${CMAKE_CURRENT_BINARY_DIR}/_deps/*.h)

  if(CMAKE_CXX_STANDARD)
    list(APPEND _opts --std=c++${CMAKE_CXX_STANDARD})
  endif()
  if(WARNINGS_AS_ERRORS)
    list(APPEND _opts --error-exitcode=2)
  endif()

  set_property(TARGET "${target}" PROPERTY CXX_CPPCHECK "${_opts}")
endfunction()

function(project_enable_include_what_you_use target)
  if(NOT TARGET "${target}")
    message(FATAL_ERROR "project_enable_include_what_you_use: target '${target}' does not exist")
  endif()
  if(NOT ENABLE_IWYU)
    return()
  endif()

  get_target_property(_type "${target}" TYPE)
  if(_type STREQUAL "INTERFACE_LIBRARY")
    return()
  endif()

  find_program(IWYU_EXECUTABLE NAMES include-what-you-use)
  if(NOT IWYU_EXECUTABLE)
    message(WARNING "include-what-you-use requested but not found")
    return()
  endif()

  set_property(TARGET "${target}" PROPERTY CXX_INCLUDE_WHAT_YOU_USE "${IWYU_EXECUTABLE}")
endfunction()

# ---------- optional global one-liners ----------

function(project_enable_global_clang_tidy)
  if(ENABLE_CLANG_TIDY)
    find_program(CLANG_TIDY_EXECUTABLE NAMES clang-tidy)
    if(CLANG_TIDY_EXECUTABLE)
      set(CMAKE_CXX_CLANG_TIDY
          "${CLANG_TIDY_EXECUTABLE}"
          -extra-arg=-Wno-unknown-warning-option
          -extra-arg=-Wno-ignored-optimization-argument
          -extra-arg=-Wno-unused-command-line-argument
          CACHE STRING "clang-tidy command line" FORCE)
      message(STATUS "clang-tidy enabled globally")
    endif()
  endif()
endfunction()

function(project_enable_global_cppcheck)
  if(ENABLE_CPPCHECK)
    find_program(CPPCHECK_EXECUTABLE NAMES cppcheck)
    if(CPPCHECK_EXECUTABLE)
      set(CMAKE_CXX_CPPCHECK
          "${CPPCHECK_EXECUTABLE}" --template=gcc --enable=style,performance,warning,portability
          CACHE STRING "cppcheck command line" FORCE)
      message(STATUS "cppcheck enabled globally")
    endif()
  endif()
endfunction()

function(project_enable_global_iwyu)
  if(ENABLE_IWYU)
    find_program(IWYU_EXECUTABLE NAMES include-what-you-use)
    if(IWYU_EXECUTABLE)
      set(CMAKE_CXX_INCLUDE_WHAT_YOU_USE
          "${IWYU_EXECUTABLE}"
          CACHE STRING "include-what-you-use command line" FORCE)
      message(STATUS "include-what-you-use enabled globally")
    endif()
  endif()
endfunction()
