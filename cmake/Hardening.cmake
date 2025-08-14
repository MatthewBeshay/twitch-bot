# cmake/Hardening.cmake
# Lightweight hardening flags, target-scoped by default.
# Provides an option to enable UBSan minimal runtime if available.

include_guard(GLOBAL)
include(CheckCXXCompilerFlag)

# Usage:
#   project_enable_hardening(<target> [UBSAN_MINIMAL_RUNTIME])
#
# The flags are chosen to be safe for general use. They are attached with
# PRIVATE scope so they do not leak to dependants.

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

  # Do not attach to INTERFACE libraries
  get_target_property(_type "${target}" TYPE)
  if(_type STREQUAL "INTERFACE_LIBRARY")
    message(TRACE "project_enable_hardening: skipping INTERFACE target '${target}'")
    return()
  endif()

  set(_cxx_opts "")
  set(_ld_opts "")
  set(_cxx_defs "")

  if(CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
    list(APPEND _cxx_opts /sdl)
    list(
      APPEND
      _ld_opts
      /NXCOMPAT
      /CETCOMPAT)
    # /guard:cf and /DYNAMICBASE are linker options on MSVC
    list(
      APPEND
      _ld_opts
      /guard:cf
      /DYNAMICBASE)
  else()
    # GLIBC++ assertions for common containers at runtime
    list(APPEND _cxx_defs -D_GLIBCXX_ASSERTIONS)

    # Strong stack protector if supported
    check_cxx_compiler_flag(-fstack-protector-strong HAS_STACK_PROTECTOR)
    if(HAS_STACK_PROTECTOR)
      list(APPEND _cxx_opts -fstack-protector-strong)
    endif()

    # Control-flow protection
    check_cxx_compiler_flag(-fcf-protection HAS_CF_PROTECTION)
    if(HAS_CF_PROTECTION)
      list(APPEND _cxx_opts -fcf-protection)
    endif()

    # Stack clash protection - GCC and recent Clang on Linux
    check_cxx_compiler_flag(-fstack-clash-protection HAS_STACK_CLASH)
    if(HAS_STACK_CLASH)
      if(UNIX)
        list(APPEND _cxx_opts -fstack-clash-protection)
      endif()
    endif()

    # Fortify level 3 for non-Debug
    list(APPEND _cxx_opts $<$<NOT:$<CONFIG:Debug>>:-U_FORTIFY_SOURCE>)
    list(APPEND _cxx_opts $<$<NOT:$<CONFIG:Debug>>:-D_FORTIFY_SOURCE=3>)

    # Optional: PIE could be added here when toolchain supports it
    # list(APPEND _cxx_opts -fpie)
    # list(APPEND _ld_opts -pie)
  endif()

  if(HARDEN_UBSAN_MINIMAL_RUNTIME
     AND NOT
         CMAKE_CXX_COMPILER_FRONTEND_VARIANT
         STREQUAL
         "MSVC")
    # Try minimal UBSan runtime without recover in target scope
    check_cxx_compiler_flag("-fsanitize=undefined -fsanitize-minimal-runtime" HAS_UBSAN_MIN)
    if(HAS_UBSAN_MIN)
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

  if(_cxx_defs)
    target_compile_definitions(${target} PRIVATE ${_cxx_defs})
  endif()
  if(_cxx_opts)
    target_compile_options(${target} PRIVATE ${_cxx_opts})
  endif()
  if(_ld_opts)
    target_link_options(${target} PRIVATE ${_ld_opts})
  endif()
endfunction()
