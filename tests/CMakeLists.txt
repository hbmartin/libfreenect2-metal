# Unit test suite for libfreenect2. Enabled with -DBUILD_TESTING=ON.
#
# Tests link against `freenect2_testlib` (a default-visibility static build of
# the library defined in the top-level CMakeLists.txt) so that internal classes
# — the CPU/Metal depth processors and the stream parsers — are reachable.

INCLUDE(FetchContent)

# The public 0.4 API requires C++17; compile tests with the same language mode.
SET(CMAKE_CXX_STANDARD 17)
SET(CMAKE_CXX_STANDARD_REQUIRED ON)

# GoogleTest. Pinned tag; fetched at configure time (the self-hosted runner has
# network access). Uses the shared C runtime to match the rest of the build.
FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG        v1.15.2
)
SET(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
SET(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
SET(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

ADD_EXECUTABLE(unit_tests
  support/synthetic.cpp
  test_registration.cpp
  test_projective_registration.cpp
  test_calibration_profile.cpp
  test_vision.cpp
  test_depth_stream_parser.cpp
  test_depth_calibration.cpp
  test_rgb_stream_parser.cpp
  test_rgb_decoder_fallback.cpp
  test_cpu_depth_processor.cpp
  test_filter_backend_parity.cpp
  test_metal_cpu_parity.cpp
  test_concurrency.cpp
  test_protocol.cpp
  test_public_api.cpp
  test_recording.cpp
  test_timing.cpp
)

TARGET_INCLUDE_DIRECTORIES(unit_tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
)
TARGET_COMPILE_FEATURES(unit_tests PRIVATE cxx_std_17)
TARGET_LINK_LIBRARIES(unit_tests PRIVATE freenect2_testlib gtest gtest_main)

# The quality options on freenect2_testlib are transitive, so sanitizer and
# coverage instrumentation propagates to the test executable automatically.

INCLUDE(GoogleTest)

# Register the tests in two disjoint groups so the Metal-vs-CPU parity cases can
# be excluded under sanitizers (which don't mix cleanly with the Metal
# framework) via `ctest -LE gpu`, while still running in the main build.
gtest_discover_tests(unit_tests
  TEST_FILTER "MetalCpuParity.*:OpenCLCpuParity.*:OpenGLCpuParity.*"
  DISCOVERY_TIMEOUT 60
  PROPERTIES LABELS "gpu"
)
gtest_discover_tests(unit_tests
  TEST_FILTER "-MetalCpuParity.*:OpenCLCpuParity.*:OpenGLCpuParity.*"
  DISCOVERY_TIMEOUT 60
  PROPERTIES LABELS "unit"
)

IF(TARGET KinectCameraCalibration)
  ADD_TEST(NAME calibration_cli_inspect
    COMMAND KinectCameraCalibration inspect
            --job "${MY_DIR}/tests/data/calibration_job_empty.json")
  SET_TESTS_PROPERTIES(calibration_cli_inspect PROPERTIES
    LABELS "unit"
    PASS_REGULAR_EXPRESSION
      "sha256=ad08e6e0bbbb28afb8c417800cad4681b9c159322a3bd2d7e8ed0421ee962f6f")
ENDIF()

IF(ENABLE_FUZZING)
  libfreenect2_add_fuzzer(fuzz_rgb_stream_parser fuzz/fuzz_rgb_stream_parser.cpp)
  libfreenect2_add_fuzzer(fuzz_depth_stream_parser fuzz/fuzz_depth_stream_parser.cpp)
  libfreenect2_add_fuzzer(fuzz_protocol_response fuzz/fuzz_protocol_response.cpp)
  libfreenect2_add_fuzzer(fuzz_recording_metadata fuzz/fuzz_recording_metadata.cpp)

  ADD_TEST(NAME fuzz_rgb_stream_parser_smoke
    COMMAND fuzz_rgb_stream_parser -runs=1000 -max_len=2097152
            -dict=${CMAKE_CURRENT_SOURCE_DIR}/fuzz/rgb.dict)
  ADD_TEST(NAME fuzz_depth_stream_parser_smoke
    COMMAND fuzz_depth_stream_parser -runs=1000 -max_len=298560
            -dict=${CMAKE_CURRENT_SOURCE_DIR}/fuzz/depth.dict)
  ADD_TEST(NAME fuzz_protocol_response_smoke
    COMMAND fuzz_protocol_response -runs=1000 -max_len=65536)
  ADD_TEST(NAME fuzz_recording_metadata_smoke
    COMMAND fuzz_recording_metadata -runs=1000 -max_len=1048576
            -dict=${CMAKE_CURRENT_SOURCE_DIR}/fuzz/recording.dict)
  SET_TESTS_PROPERTIES(
    fuzz_rgb_stream_parser_smoke
    fuzz_depth_stream_parser_smoke
    fuzz_protocol_response_smoke
    fuzz_recording_metadata_smoke
    PROPERTIES LABELS "fuzz" TIMEOUT 120
  )
ENDIF()
