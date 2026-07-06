# Unit test suite for libfreenect2. Enabled with -DBUILD_TESTING=ON.
#
# Tests link against `freenect2_testlib` (a default-visibility static build of
# the library defined in the top-level CMakeLists.txt) so that internal classes
# — the CPU/Metal depth processors and the stream parsers — are reachable.

INCLUDE(FetchContent)

# GoogleTest. Pinned tag; fetched at configure time (the self-hosted runner has
# network access). Uses the shared C runtime to match the rest of the build.
FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG        v1.15.2
)
SET(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

ADD_EXECUTABLE(unit_tests
  support/synthetic.cpp
  test_registration.cpp
  test_depth_stream_parser.cpp
  test_rgb_stream_parser.cpp
  test_cpu_depth_processor.cpp
  test_metal_cpu_parity.cpp
)

TARGET_INCLUDE_DIRECTORIES(unit_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
TARGET_LINK_LIBRARIES(unit_tests PRIVATE freenect2_testlib gtest gtest_main)

# The sanitizer flags on freenect2_testlib are PUBLIC, so they propagate to the
# test executable automatically when ENABLE_SANITIZERS=ON.

INCLUDE(GoogleTest)

# Register the tests in two disjoint groups so the Metal-vs-CPU parity cases can
# be excluded under sanitizers (which don't mix cleanly with the Metal
# framework) via `ctest -LE gpu`, while still running in the main build.
gtest_discover_tests(unit_tests
  TEST_FILTER "MetalCpuParity.*"
  PROPERTIES LABELS "gpu"
)
gtest_discover_tests(unit_tests
  TEST_FILTER "-MetalCpuParity.*"
  PROPERTIES LABELS "unit"
)
