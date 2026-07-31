# Centralized, target-scoped quality instrumentation for libfreenect2.
#
# Keep these settings off third-party targets fetched by the test suite. Apply
# them explicitly with libfreenect2_apply_quality() to code owned by this
# project.

OPTION(ENABLE_STRICT_WARNINGS "Enable the project's high-signal compiler warnings" ON)
OPTION(ENABLE_WARNINGS_AS_ERRORS "Treat project compiler warnings as errors" OFF)
OPTION(ENABLE_ASAN "Build project targets with AddressSanitizer" OFF)
OPTION(ENABLE_UBSAN "Build project targets with UndefinedBehaviorSanitizer" OFF)
OPTION(ENABLE_UBSAN_IMPLICIT_CONVERSIONS "Add Clang's implicit integer conversion checks" OFF)
OPTION(ENABLE_TSAN "Build project targets with ThreadSanitizer" OFF)
OPTION(ENABLE_COVERAGE "Build project targets with LLVM source coverage" OFF)
OPTION(ENABLE_FUZZING "Build libFuzzer targets (Clang only)" OFF)
OPTION(ENABLE_STDLIB_HARDENING "Enable standard-library runtime assertions" OFF)

# Backward compatibility for callers using the original combined option.
IF(ENABLE_SANITIZERS)
  SET(ENABLE_ASAN ON CACHE BOOL "Build project targets with AddressSanitizer" FORCE)
  SET(ENABLE_UBSAN ON CACHE BOOL "Build project targets with UndefinedBehaviorSanitizer" FORCE)
ENDIF()

IF(ENABLE_FUZZING)
  SET(BUILD_TESTING ON CACHE BOOL "Build the unit test suite" FORCE)
  SET(ENABLE_ASAN ON CACHE BOOL "Build project targets with AddressSanitizer" FORCE)
  SET(ENABLE_UBSAN ON CACHE BOOL "Build project targets with UndefinedBehaviorSanitizer" FORCE)
ENDIF()

IF(ENABLE_UBSAN_IMPLICIT_CONVERSIONS)
  IF(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    MESSAGE(FATAL_ERROR "Implicit-conversion sanitizing requires Clang")
  ENDIF()
  SET(ENABLE_UBSAN ON CACHE BOOL "Build project targets with UndefinedBehaviorSanitizer" FORCE)
ENDIF()

IF(ENABLE_TSAN AND ENABLE_ASAN)
  MESSAGE(FATAL_ERROR "ThreadSanitizer and AddressSanitizer require separate builds")
ENDIF()

IF((ENABLE_ASAN OR ENABLE_UBSAN OR ENABLE_TSAN OR ENABLE_COVERAGE OR ENABLE_FUZZING)
   AND NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  MESSAGE(FATAL_ERROR "The requested sanitizer/coverage/fuzzing profile requires Clang or GCC")
ENDIF()

IF((ENABLE_COVERAGE OR ENABLE_FUZZING) AND NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  MESSAGE(FATAL_ERROR "LLVM coverage and libFuzzer profiles require Clang")
ENDIF()

ADD_LIBRARY(libfreenect2_quality_options INTERFACE)

IF(ENABLE_STRICT_WARNINGS)
  IF(MSVC)
    TARGET_COMPILE_OPTIONS(libfreenect2_quality_options INTERFACE
      "$<$<COMPILE_LANGUAGE:CXX>:/W4>"
    )
  ELSEIF(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    TARGET_COMPILE_OPTIONS(libfreenect2_quality_options INTERFACE
      "$<$<COMPILE_LANGUAGE:CXX,OBJCXX>:-Wall;-Wextra;-Wformat=2;-Wshadow;-Wundef>"
    )
  ENDIF()
ENDIF()

IF(ENABLE_WARNINGS_AS_ERRORS)
  IF(MSVC)
    TARGET_COMPILE_OPTIONS(libfreenect2_quality_options INTERFACE
      "$<$<COMPILE_LANGUAGE:CXX>:/WX>"
    )
  ELSEIF(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    TARGET_COMPILE_OPTIONS(libfreenect2_quality_options INTERFACE
      "$<$<COMPILE_LANGUAGE:CXX,OBJCXX>:-Werror>"
    )
  ENDIF()
ENDIF()

SET(_libfreenect2_sanitizers)
IF(ENABLE_ASAN)
  LIST(APPEND _libfreenect2_sanitizers address)
ENDIF()
IF(ENABLE_UBSAN)
  LIST(APPEND _libfreenect2_sanitizers undefined)
ENDIF()
IF(ENABLE_UBSAN_IMPLICIT_CONVERSIONS)
  LIST(APPEND _libfreenect2_sanitizers implicit-integer-conversion)
ENDIF()
IF(ENABLE_TSAN)
  LIST(APPEND _libfreenect2_sanitizers thread)
ENDIF()

IF(_libfreenect2_sanitizers)
  STRING(REPLACE ";" "," _libfreenect2_sanitizer_set "${_libfreenect2_sanitizers}")
  TARGET_COMPILE_OPTIONS(libfreenect2_quality_options INTERFACE
    "$<$<COMPILE_LANGUAGE:CXX,OBJCXX>:-fsanitize=${_libfreenect2_sanitizer_set};-fno-omit-frame-pointer>"
  )
  TARGET_LINK_OPTIONS(libfreenect2_quality_options INTERFACE
    -fsanitize=${_libfreenect2_sanitizer_set}
  )
ENDIF()

IF(ENABLE_FUZZING)
  # Instrument the library without adding a fuzzer main() to ordinary targets.
  TARGET_COMPILE_OPTIONS(libfreenect2_quality_options INTERFACE
    "$<$<COMPILE_LANGUAGE:CXX,OBJCXX>:-fsanitize=fuzzer-no-link>"
  )
ENDIF()

IF(ENABLE_COVERAGE)
  TARGET_COMPILE_OPTIONS(libfreenect2_quality_options INTERFACE
    "$<$<COMPILE_LANGUAGE:CXX,OBJCXX>:-fprofile-instr-generate;-fcoverage-mapping>"
  )
  TARGET_LINK_OPTIONS(libfreenect2_quality_options INTERFACE
    -fprofile-instr-generate -fcoverage-mapping
  )
ENDIF()

IF(ENABLE_STDLIB_HARDENING)
  IF(APPLE AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    TARGET_COMPILE_DEFINITIONS(libfreenect2_quality_options INTERFACE
      _LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_EXTENSIVE
    )
  ELSE()
    TARGET_COMPILE_DEFINITIONS(libfreenect2_quality_options INTERFACE
      _GLIBCXX_ASSERTIONS
    )
  ENDIF()
ENDIF()

FUNCTION(libfreenect2_apply_quality TARGET_NAME VISIBILITY)
  IF(NOT TARGET ${TARGET_NAME})
    MESSAGE(FATAL_ERROR "Cannot apply quality checks to missing target '${TARGET_NAME}'")
  ENDIF()
  # Most long-standing project targets still use target_link_libraries' plain
  # signature. CMake forbids mixing that with the keyword signature, so keep
  # this helper compatible with those targets. Plain links are transitive by
  # default, which preserves the PUBLIC behavior needed by freenect2_testlib.
  TARGET_LINK_LIBRARIES(${TARGET_NAME} libfreenect2_quality_options)
ENDFUNCTION()

FUNCTION(libfreenect2_add_fuzzer TARGET_NAME SOURCE_FILE)
  IF(NOT ENABLE_FUZZING)
    RETURN()
  ENDIF()

  ADD_EXECUTABLE(${TARGET_NAME} ${SOURCE_FILE})
  TARGET_LINK_LIBRARIES(${TARGET_NAME} PRIVATE freenect2_testlib)
  TARGET_COMPILE_OPTIONS(${TARGET_NAME} PRIVATE -fsanitize=fuzzer)
  TARGET_LINK_OPTIONS(${TARGET_NAME} PRIVATE -fsanitize=fuzzer,address,undefined)
  SET_TARGET_PROPERTIES(${TARGET_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/fuzz"
  )
ENDFUNCTION()
