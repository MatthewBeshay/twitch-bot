# cmake/Hardening.cmake

include_guard(GLOBAL)

include(CheckCXXCompilerFlag)
include(CheckLinkerFlag)

function(project_enable_hardening target)
  if(NOT TARGET "${target}")
    message(FATAL_ERROR "project_enable_hardening: target '${target}' does not exist")
  endif()

  set(options UBSAN_MINIMAL_RUNTIME)
  cmake_parse_arguments(
    HARDEN
    "${options}"
    ""
    ""
    ${ARGN})

  get_target_property(_type "${target}" TYPE)
  if(_type STREQUAL "INTERFACE_LIBRARY")
    message(TRACE "project_enable_hardening: skipping INTERFACE target '${target}'")
    return()
  endif()

  set(_cxx_opts "")
  set(_ld_opts "")
  set(_cxx_defs "")

  function(_append_cxx_if_ok _flag _out)
    string(MAKE_C_IDENTIFIER "HAVE_${_flag}" _var)
    check_cxx_compiler_flag("${_flag}" ${_var})
    if(${_var})
      set(${_out}
          ${${_out}} "${_flag}"
          PARENT_SCOPE)
    endif()
  endfunction()

  function(_append_ld_if_ok _flag _out)
    string(MAKE_C_IDENTIFIER "HAVE_LD_${_flag}" _var)
    check_linker_flag(CXX "${_flag}" ${_var})
    if(${_var})
      set(${_out}
          ${${_out}} "${_flag}"
          PARENT_SCOPE)
    endif()
  endfunction()

  if(CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
    list(APPEND _cxx_opts /sdl)

    _append_cxx_if_ok("/Qspectre" _cxx_opts)

    _append_ld_if_ok("/guard:cf" _ld_opts)
    _append_ld_if_ok("/DYNAMICBASE" _ld_opts)
    _append_ld_if_ok("/NXCOMPAT" _ld_opts)
    _append_ld_if_ok("/CETCOMPAT" _ld_opts)
    _append_ld_if_ok("/HIGHENTROPYVA" _ld_opts)

  else()
    list(APPEND _cxx_defs _GLIBCXX_ASSERTIONS)

    _append_cxx_if_ok("-fstack-protector-strong" _cxx_opts)

    _append_cxx_if_ok("-fcf-protection" _cxx_opts)

    if(UNIX)
      _append_cxx_if_ok("-fstack-clash-protection" _cxx_opts)
    endif()

    _append_cxx_if_ok("-U_FORTIFY_SOURCE" _cxx_opts)
    _append_cxx_if_ok("-D_FORTIFY_SOURCE=3" _cxx_opts)

    if(_type STREQUAL "EXECUTABLE")
      _append_cxx_if_ok("-fPIE" _cxx_opts)
      _append_ld_if_ok("-pie" _ld_opts)
    endif()

    _append_ld_if_ok("-Wl,-z,relro" _ld_opts)
    _append_ld_if_ok("-Wl,-z,now" _ld_opts)
    _append_ld_if_ok("-Wl,-z,noexecstack" _ld_opts)
    _append_ld_if_ok("-Wl,--as-needed" _ld_opts)
  endif()

  if(HARDEN_UBSAN_MINIMAL_RUNTIME
     AND NOT
         CMAKE_CXX_COMPILER_FRONTEND_VARIANT
         STREQUAL
         "MSVC")
    check_cxx_compiler_flag("-fsanitize=undefined -fsanitize-minimal-runtime -fno-sanitize-recover=undefined"
                            HAVE_UBSAN_MIN)
    if(HAVE_UBSAN_MIN)
      list(
        APPEND
        _cxx_opts
        -fsanitize=undefined
        -fsanitize-minimal-runtime
        -fno-sanitize-recover=undefined)
      list(
        APPEND
        _ld_opts
        -fsanitize=undefined
        -fsanitize-minimal-runtime
        -fno-sanitize-recover=undefined)
    else()
      message(STATUS "UBSan minimal runtime not supported by this toolchain")
    endif()
  endif()

  set(_cxx_opts_dbg "")
  set(_cxx_opts_rel "")
  foreach(f IN LISTS _cxx_opts)
    list(APPEND _cxx_opts_rel "$<$<NOT:$<CONFIG:Debug>>:${f}>")
  endforeach()

  if(_cxx_defs)
    target_compile_definitions(${target} PRIVATE ${_cxx_defs})
  endif()
  if(_cxx_opts_rel)
    target_compile_options(${target} PRIVATE ${_cxx_opts_rel})
  endif()

  if(_ld_opts)
    target_link_options(${target} PRIVATE ${_ld_opts})
  endif()
endfunction()
