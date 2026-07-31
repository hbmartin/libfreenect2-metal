# C++ quality checks

libfreenect2 keeps compiler diagnostics and dynamic instrumentation target-scoped. Third-party
GoogleTest code and system headers are not subjected to the project's warning policy.

## Everyday build

High-signal warnings are enabled by default. To use the same warning gate as CI:

```sh
cmake -S . -B build-quality -G Ninja \
  -DBUILD_TESTING=ON \
  -DENABLE_WARNINGS_AS_ERRORS=ON
cmake --build build-quality
ctest --test-dir build-quality --output-on-failure
```

Set `ENABLE_STRICT_WARNINGS=OFF` only when diagnosing a compiler that does not understand the
project warning set. Do not use it to land warning-producing first-party code.

## Sanitizers and hardening

Each profile needs a separate build directory. ASan and TSan cannot be combined.

```sh
# Address, leak, and undefined-behavior checks
cmake -S . -B build-asan -G Ninja -DBUILD_TESTING=ON \
  -DENABLE_ASAN=ON -DENABLE_UBSAN=ON
cmake --build build-asan
ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 \
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
  ctest --test-dir build-asan -L unit --output-on-failure

# Data-race checks
cmake -S . -B build-tsan -G Ninja -DBUILD_TESTING=ON \
  -DENABLE_TSAN=ON
cmake --build build-tsan
TSAN_OPTIONS=halt_on_error=1 \
  ctest --test-dir build-tsan -L unit --output-on-failure

# Standard-library precondition assertions
cmake -S . -B build-hardened -G Ninja -DBUILD_TESTING=ON \
  -DENABLE_STDLIB_HARDENING=ON
```

`ENABLE_SANITIZERS=ON` remains as a compatibility alias for the combined ASan+UBSan profile.
The scheduled deep-analysis workflow also runs Clang's implicit-integer-conversion sanitizer.

## Fuzzing

Clang's libFuzzer drives the RGB stream parser, depth stream parser, and binary protocol response
decoders. Every pull request runs a bounded smoke test; longer local runs retain interesting inputs
in a corpus directory:

```sh
CC=clang CXX=clang++ cmake -S . -B build-fuzz -G Ninja \
  -DENABLE_FUZZING=ON -DBUILD_EXAMPLES=OFF
cmake --build build-fuzz --target \
  fuzz_rgb_stream_parser fuzz_depth_stream_parser fuzz_protocol_response

mkdir -p build-fuzz/corpus/rgb
build-fuzz/bin/fuzz/fuzz_rgb_stream_parser \
  build-fuzz/corpus/rgb -max_total_time=300 \
  -dict=tests/fuzz/rgb.dict -max_len=2097152
```

Any crashing input should be committed as a regression-test fixture after minimizing it with the
fuzzer's `-minimize_crash=1` mode.

## Coverage

The CI coverage job enforces the current ratcheting floors of 30% line coverage and 25% branch
coverage for first-party CPU code. Raise these values as lifecycle, USB, and replay tests are added;
do not lower them to accommodate a change.

```sh
CC=clang CXX=clang++ cmake -S . -B build-coverage -G Ninja \
  -DBUILD_TESTING=ON -DENABLE_COVERAGE=ON
cmake --build build-coverage
LLVM_PROFILE_FILE="$PWD/build-coverage/unit-%p.profraw" \
  ctest --test-dir build-coverage -L unit --output-on-failure
```

The workflow publishes the complete `llvm-cov` text and LCOV reports as artifacts.

## Static analysis

The required CI job runs `.clang-tidy` over every first-party translation unit in CMake's compile
database. It enables Clang Static Analyzer, bug-prone, concurrency, performance, and portability
checks. CodeQL's `security-extended` query suite runs independently. The scheduled deep-analysis
workflow also runs Clang `scan-build` over a C++11 configuration.

For a local run:

```sh
cmake -S . -B build-tidy -G Ninja \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DBUILD_TESTING=ON -DBUILD_EXAMPLES=ON
cmake --build build-tidy
CLANG_TIDY=/path/to/clang-tidy \
  python3 tools/quality/run_clang_tidy.py build-tidy
```

The focused Semgrep policy prevents direct signed indexing of the enumerated-device collection:

```sh
semgrep --config tools/quality/semgrep.yml
```

## Hardware soak

The manually dispatched `Kinect hardware soak` workflow requires a self-hosted runner with the
labels `macOS` and `kinect`, a Kinect v2, and a usable Metal device. It builds the shipping library
and Protonect with ASan+UBSan, enables the Metal debug layer, and repeatedly exercises CPU and Metal
connect/start/capture/stop/close lifecycles.
