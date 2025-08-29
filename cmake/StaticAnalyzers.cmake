# cmake/StaticAnalyzers.cmake

include_guard(GLOBAL)

option(ENABLE_CLANG_TIDY "Enable clang-tidy for selected targets" OFF)
option(ENABLE_CPPCHECK "Enable cppcheck for selected targets" OFF)
option(ENABLE_IWYU "Enable include-what-you-use for selected targets" OFF)

set(CLANG_TIDY_ARGS
    ""
    CACHE STRING "Extra args for clang-tidy")
set(CPPCHECK_ARGS
    ""
    CACHE STRING "Extra args for cppcheck")
set(IWYU_ARGS
    ""
    CACHE STRING "Extra args for include-what-you-use")
set(IWYU_MAPPING_FILE
    ""
    CACHE FILEPATH "Optional IWYU mapping file (.imp)")

function(_sa_warn_if_no_compile_commands)
  if(NOT CMAKE_EXPORT_COMPILE_COMMANDS)
    message(STATUS "Static analysis works best with CMAKE_EXPORT_COMPILE_COMMANDS=ON")
  endif()
endfunction()

function(_sa_is_interface target out_var)
  get_target_property(_type "${target}" TYPE)
  if(_type STREQUAL "INTERFACE_LIBRARY")
    set(${out_var}
        TRUE
        PARENT_SCOPE)
  else()
    set(${out_var}
        FALSE
        PARENT_SCOPE)
  endif()
endfunction()

function(project_enable_clang_tidy target)
  if(NOT TARGET "${target}")
    message(FATAL_ERROR "project_enable_clang_tidy: target '${target}' does not exist")
  endif()
  if(NOT ENABLE_CLANG_TIDY)
    return()
  endif()

  _sa_is_interface("${target}" _is_iface)
  if(_is_iface)
    return()
  endif()

  find_program(CLANG_TIDY_EXECUTABLE NAMES clang-tidy)
  if(NOT CLANG_TIDY_EXECUTABLE)
    message(WARNING "clang-tidy requested but not found")
    return()
  endif()

  _sa_warn_if_no_compile_commands()

  set(_opts "${CLANG_TIDY_EXECUTABLE}")

  list(
    APPEND
    _opts
    -extra-arg=-Wno-unknown-warning-option
    -extra-arg=-Wno-ignored-optimization-argument
    -extra-arg=-Wno-unused-command-line-argument)

  if(CMAKE_CXX_STANDARD)
    if(CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
      list(APPEND _opts -extra-arg-before=/std:c++${CMAKE_CXX_STANDARD})
    else()
      list(APPEND _opts -extra-arg-before=-std=c++${CMAKE_CXX_STANDARD})
    endif()
  endif()

  if(WARNINGS_AS_ERRORS)
    list(APPEND _opts -warnings-as-errors=*)
  endif()

  if(CLANG_TIDY_ARGS)
    separate_arguments(_extra NATIVE_COMMAND "${CLANG_TIDY_ARGS}")
    list(APPEND _opts ${_extra})
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

  _sa_is_interface("${target}" _is_iface)
  if(_is_iface)
    return()
  endif()

  find_program(CPPCHECK_EXECUTABLE NAMES cppcheck)
  if(NOT CPPCHECK_EXECUTABLE)
    message(WARNING "cppcheck requested but not found")
    return()
  endif()

  _sa_warn_if_no_compile_commands()

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

  if(EXISTS "${CMAKE_BINARY_DIR}/compile_commands.json")
    list(APPEND _opts --project="${CMAKE_BINARY_DIR}/compile_commands.json")
  endif()

  if(CMAKE_CXX_STANDARD)
    list(APPEND _opts --std=c++${CMAKE_CXX_STANDARD})
  endif()
  if(WARNINGS_AS_ERRORS)
    list(APPEND _opts --error-exitcode=2)
  endif()

  if(CPPCHECK_ARGS)
    separate_arguments(_extra NATIVE_COMMAND "${CPPCHECK_ARGS}")
    list(APPEND _opts ${_extra})
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

  _sa_is_interface("${target}" _is_iface)
  if(_is_iface)
    return()
  endif()

  find_program(IWYU_EXECUTABLE NAMES include-what-you-use iwyu)
  if(NOT IWYU_EXECUTABLE)
    message(WARNING "include-what-you-use requested but not found")
    return()
  endif()

  set(_opts "${IWYU_EXECUTABLE}")
  if(IWYU_MAPPING_FILE)
    list(APPEND _opts --mapping_file="${IWYU_MAPPING_FILE}")
  endif()
  if(IWYU_ARGS)
    separate_arguments(_extra NATIVE_COMMAND "${IWYU_ARGS}")
    list(APPEND _opts ${_extra})
  endif()

  set_property(TARGET "${target}" PROPERTY C_INCLUDE_WHAT_YOU_USE "${_opts}")
  set_property(TARGET "${target}" PROPERTY CXX_INCLUDE_WHAT_YOU_USE "${_opts}")
endfunction()

function(project_enable_global_clang_tidy)
  if(NOT ENABLE_CLANG_TIDY)
    return()
  endif()
  find_program(CLANG_TIDY_EXECUTABLE NAMES clang-tidy)
  if(NOT CLANG_TIDY_EXECUTABLE)
    return()
  endif()

  _sa_warn_if_no_compile_commands()

  set(_opts
      "${CLANG_TIDY_EXECUTABLE}"
      -extra-arg=-Wno-unknown-warning-option
      -extra-arg=-Wno-ignored-optimization-argument
      -extra-arg=-Wno-unused-command-line-argument)
  if(CLANG_TIDY_ARGS)
    separate_arguments(_extra NATIVE_COMMAND "${CLANG_TIDY_ARGS}")
    list(APPEND _opts ${_extra})
  endif()

  set(CMAKE_CXX_CLANG_TIDY
      "${_opts}"
      CACHE STRING "clang-tidy command line" FORCE)
  message(STATUS "clang-tidy enabled globally")
endfunction()

function(project_enable_global_cppcheck)
  if(NOT ENABLE_CPPCHECK)
    return()
  endif()
  find_program(CPPCHECK_EXECUTABLE NAMES cppcheck)
  if(NOT CPPCHECK_EXECUTABLE)
    return()
  endif()

  _sa_warn_if_no_compile_commands()

  set(_opts "${CPPCHECK_EXECUTABLE}" --template=gcc --enable=style,performance,warning,portability)
  if(EXISTS "${CMAKE_BINARY_DIR}/compile_commands.json")
    list(APPEND _opts --project="${CMAKE_BINARY_DIR}/compile_commands.json")
  endif()
  if(CPPCHECK_ARGS)
    separate_arguments(_extra NATIVE_COMMAND "${CPPCHECK_ARGS}")
    list(APPEND _opts ${_extra})
  endif()

  set(CMAKE_CXX_CPPCHECK
      "${_opts}"
      CACHE STRING "cppcheck command line" FORCE)
  message(STATUS "cppcheck enabled globally")
endfunction()

function(project_enable_global_iwyu)
  if(NOT ENABLE_IWYU)
    return()
  endif()
  find_program(IWYU_EXECUTABLE NAMES include-what-you-use iwyu)
  if(NOT IWYU_EXECUTABLE)
    return()
  endif()

  set(_opts "${IWYU_EXECUTABLE}")
  if(IWYU_MAPPING_FILE)
    list(APPEND _opts --mapping_file="${IWYU_MAPPING_FILE}")
  endif()
  if(IWYU_ARGS)
    separate_arguments(_extra NATIVE_COMMAND "${IWYU_ARGS}")
    list(APPEND _opts ${_extra})
  endif()

  set(CMAKE_CXX_INCLUDE_WHAT_YOU_USE
      "${_opts}"
      CACHE STRING "include-what-you-use command line" FORCE)
  set(CMAKE_C_INCLUDE_WHAT_YOU_USE
      "${_opts}"
      CACHE STRING "include-what-you-use command line" FORCE)
  message(STATUS "include-what-you-use enabled globally")
endfunction()
