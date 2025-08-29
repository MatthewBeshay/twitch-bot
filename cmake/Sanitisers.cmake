# cmake/Sanitisers.cmake

include_guard(GLOBAL)

if(NOT DEFINED ENABLE_SANITISERS)
  option(ENABLE_SANITISERS "Enable sanitisers for Debug builds" ON)
endif()
if(NOT DEFINED SANITISE_ADDRESS)
  option(SANITISE_ADDRESS "Enable AddressSanitiser" ON)
endif()
if(NOT DEFINED SANITISE_UNDEFINED)
  option(SANITISE_UNDEFINED "Enable UndefinedBehaviourSanitiser" ON)
endif()
if(NOT DEFINED SANITISE_LEAK)
  option(SANITISE_LEAK "Enable LeakSanitiser" OFF)
endif()
if(NOT DEFINED SANITISE_THREAD)
  option(SANITISE_THREAD "Enable ThreadSanitiser" OFF)
endif()
if(NOT DEFINED SANITISE_MEMORY)
  option(SANITISE_MEMORY "Enable MemorySanitiser (Clang only, specialised use)" OFF)
endif()
if(NOT DEFINED SANITISERS_DISABLE_IPO)
  option(SANITISERS_DISABLE_IPO "Disable IPO/LTO on targets with sanitisers enabled" ON)
endif()

if(NOT DEFINED _PROJECT_MSVC_ASAN_NOTE_EMITTED)
  set(_PROJECT_MSVC_ASAN_NOTE_EMITTED
      FALSE
      CACHE INTERNAL "MSVC ASan note emitted" FORCE)
endif()

function(_sanitiser_validate)
  if(SANITISE_THREAD AND (SANITISE_ADDRESS OR SANITISE_LEAK))
    message(FATAL_ERROR "ThreadSanitiser cannot be combined with AddressSanitiser or LeakSanitiser")
  endif()
  if(SANITISE_MEMORY
     AND (SANITISE_ADDRESS
          OR SANITISE_LEAK
          OR SANITISE_THREAD))
    message(FATAL_ERROR "MemorySanitiser cannot be combined with Address, Leak or Thread sanitisers")
  endif()
  if(SANITISE_MEMORY
     AND NOT
         CMAKE_CXX_COMPILER_ID
         MATCHES
         ".*Clang")
    message(WARNING "MemorySanitiser requested but compiler is not Clang - ignoring")
  endif()
endfunction()

function(project_enable_sanitisers target)
  if(NOT TARGET "${target}")
    message(FATAL_ERROR "project_enable_sanitisers: target '${target}' does not exist")
  endif()
  if(NOT ENABLE_SANITISERS)
    return()
  endif()

  get_target_property(_type "${target}" TYPE)
  if(_type STREQUAL "INTERFACE_LIBRARY")
    return()
  endif()

  _sanitiser_validate()

  set(_scope PRIVATE)
  set(_dbg $<CONFIG:Debug>)
  set(_san_enabled FALSE)

  if(SANITISERS_DISABLE_IPO)
    set_property(TARGET ${target} PROPERTY INTERPROCEDURAL_OPTIMIZATION FALSE)
  endif()

  if(CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
    if(SANITISE_ADDRESS)
      target_compile_options(
        ${target}
        ${_scope}
        $<${_dbg}:/fsanitize=address>
        $<${_dbg}:/Zi>
        $<${_dbg}:/d2Zi+>)
      target_link_options(
        ${target}
        ${_scope}
        $<${_dbg}:/INCREMENTAL:NO>
        $<${_dbg}:/fsanitize=address>)
      target_compile_definitions(${target} ${_scope} $<${_dbg}:_DISABLE_VECTOR_ANNOTATION>
                                           $<${_dbg}:_DISABLE_STRING_ANNOTATION>)
      set(_san_enabled TRUE)
      if(NOT _PROJECT_MSVC_ASAN_NOTE_EMITTED)
        message(STATUS "Enabling AddressSanitiser for Debug on MSVC/clang-cl toolchain")
        set(_PROJECT_MSVC_ASAN_NOTE_EMITTED
            TRUE
            CACHE INTERNAL "MSVC ASan note emitted" FORCE)
      endif()
    endif()

    if(SANITISE_LEAK
       OR SANITISE_UNDEFINED
       OR SANITISE_THREAD
       OR SANITISE_MEMORY)
      if(NOT _PROJECT_MSVC_ASAN_NOTE_EMITTED)
        message(STATUS "MSVC/clang-cl supports AddressSanitiser; other sanitisers are ignored on Windows")
        set(_PROJECT_MSVC_ASAN_NOTE_EMITTED
            TRUE
            CACHE INTERNAL "MSVC ASan note emitted" FORCE)
      endif()
    endif()

  else()
    set(_san_list "")
    if(SANITISE_ADDRESS)
      list(APPEND _san_list address)
    endif()
    if(SANITISE_UNDEFINED)
      list(APPEND _san_list undefined)
    endif()
    if(SANITISE_LEAK)
      list(APPEND _san_list leak)
    endif()
    if(SANITISE_THREAD)
      list(APPEND _san_list thread)
    endif()
    if(SANITISE_MEMORY AND CMAKE_CXX_COMPILER_ID MATCHES ".*Clang")
      list(APPEND _san_list memory)
    endif()

    if(_san_list)
      string(
        REPLACE ";"
                ","
                _san_csv
                "${_san_list}")
      target_compile_options(
        ${target}
        ${_scope}
        $<${_dbg}:-fsanitize=${_san_csv}>
        $<${_dbg}:-fno-omit-frame-pointer>)
      target_link_options(${target} ${_scope} $<${_dbg}:-fsanitize=${_san_csv}>)
      set(_san_enabled TRUE)
    endif()
  endif()

  if(_san_enabled)
    set_property(TARGET ${target} PROPERTY TB_SANITIZED TRUE)
  endif()
endfunction()
