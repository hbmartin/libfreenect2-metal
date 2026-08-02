# Development and contributing {#development}

Everything needed to build, test, and contribute to libfreenect2 itself. For
installing the library to *use* it, see @ref install_macos, @ref install_linux,
or @ref install_windows.

## Toolchain requirements

| Tool | Version | Needed for |
|---|---|---|
| CMake | 3.16 or newer | building |
| C++ compiler | C++11 (the CI floor); newer standards work | building |
| Ninja | any recent | recommended generator; CI uses it |
| Python | **3.12 or newer** | repository Python tools and tests |
| uv | 0.12.x | Python dependency management |
| clang-format | 18 recommended | formatting C++ |
| clang-tidy | 18 | static analysis |
| Doxygen | any recent | building the documentation site |

Python is not needed to build or use the library. It is only needed for the
repository's own tooling under `tools/` and `tests/python`.

## Python tooling

The repository's Python helper and maintenance scripts require **Python 3.12 or
newer**, pinned by `.python-version` and enforced by `requires-python` in
`pyproject.toml`. Dependencies are managed with `uv`.

```sh
uv sync --locked
uv run ruff check .
uv run ruff format --check .
uv run pytest
```

Those four commands are exactly the CI Python gate. The Ruff policy matches
`pylibfreenect3`: it targets Python 3.12, uses an 88-character line length, and
enables the `E`, `F`, `I`, `UP`, `B`, `SIM`, `RUF`, and `PT` rule families. See
@ref quality for the full rationale.

## Building for development

Build with the test suite and the CI warning gate enabled:

```sh
cmake -S . -B build-dev -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DBUILD_TESTING=ON \
  -DBUILD_EXAMPLES=ON \
  -DENABLE_WARNINGS_AS_ERRORS=ON
cmake --build build-dev
ctest --test-dir build-dev --output-on-failure
```

`BUILD_TESTING=ON` also builds `freenect2_testlib`, a second static library
compiled from the same sources with default symbol visibility. The shipping
`freenect2` target uses hidden visibility, so its internal classes are not
linkable from a test binary; the tests link the testlib instead. The shipping
library is left untouched.

Unit tests are labelled, so `ctest -L unit` selects the fast subset — which is
what the sanitizer profiles run.

## Quality and instrumentation options

These are development-only build options, defined in
`cmake_modules/QualityChecks.cmake` and applied only to first-party targets.
Third-party code fetched by the test suite is never instrumented.

| Option | Default | Effect |
|---|---|---|
| `ENABLE_STRICT_WARNINGS` | `ON` | The project's high-signal compiler warnings |
| `ENABLE_WARNINGS_AS_ERRORS` | `OFF` | Treat those warnings as errors (CI uses `ON`) |
| `ENABLE_ASAN` | `OFF` | AddressSanitizer |
| `ENABLE_UBSAN` | `OFF` | UndefinedBehaviorSanitizer |
| `ENABLE_UBSAN_IMPLICIT_CONVERSIONS` | `OFF` | Clang's implicit integer conversion checks |
| `ENABLE_TSAN` | `OFF` | ThreadSanitizer (cannot be combined with ASan) |
| `ENABLE_COVERAGE` | `OFF` | LLVM source coverage (Clang only) |
| `ENABLE_FUZZING` | `OFF` | libFuzzer targets (Clang only; forces `BUILD_TESTING` and `ENABLE_ASAN`) |
| `ENABLE_STDLIB_HARDENING` | `OFF` | Standard-library runtime assertions |
| `ENABLE_SANITIZERS` | `OFF` | Compatibility alias that forces `ENABLE_ASAN` + `ENABLE_UBSAN` |
| `ENABLE_PROFILING` | `OFF` | Collect profiling stats (memory consuming) |

Each profile needs its own build directory. Full recipes — including the
`ASAN_OPTIONS`/`TSAN_OPTIONS` the CI uses, fuzzing corpora, and coverage report
generation — are in @ref quality.

Do not set `ENABLE_STRICT_WARNINGS=OFF` to land warning-producing first-party
code; it exists only for diagnosing a compiler that does not understand the
project warning set.

## Formatting and static analysis

C++ formatting is governed by `.clang-format`. CI runs an advisory
`git clang-format` check over changed lines only:

```sh
git clang-format --diff origin/master HEAD
```

`clang-tidy` runs against a compile database (`.clang-tidy` holds the checks),
and a focused Semgrep policy lives at `tools/quality/semgrep.yml`:

```sh
cmake -S . -B build-tidy -G Ninja -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
semgrep scan --config tools/quality/semgrep.yml --error --metrics=off
```

## Building the documentation

The site is Doxygen with the doxygen-awesome theme. With Doxygen installed, the
`doc` target is added automatically:

```sh
cmake -S . -B build-doc
cmake --build build-doc --target doc
```

Output lands under `build-doc/doc/html`. The published site is built from
`master` by the `Documentation` workflow and served from
<https://hbmartin.github.io/libfreenect2-metal/>.

When adding a guide, add the file to both `doc/Doxyfile.in`'s `INPUT` list and
the relevant `@par` section of `doc/guides.dox`, and give its title a Doxygen
anchor (`# Title {#anchor}`) so other pages can `@ref` it.

## Continuous integration

The `CI` workflow runs on every push to `master` and every pull request:

| Job | What it covers |
|---|---|
| `python-quality` | Ruff lint, Ruff format, pytest |
| `build-test-metal` | AppleClang on self-hosted macOS: C++11, Metal, warnings-as-errors, hardened stdlib, Metal/CPU parity tests |
| `linux-builds` | GCC and Clang × shared and static |
| `linux-filter-backends` | Depth filter backend combinations |
| `linux-vaapi` | VAAPI JPEG decoding |
| `sanitizers` | ASan + UBSan, TSan |
| `fuzzers` | Bounded libFuzzer smoke run |
| `coverage` | LLVM source coverage |
| `format` | `git clang-format` on changed lines (advisory) |
| `semgrep` | Focused policy scan |
| `static-analysis` | clang-tidy 18 |

Pull requests from forks never execute on the persistent self-hosted runner, so
the Metal job is skipped there. See @ref self_hosted_runner for that runner's
setup. Additional scheduled workflows cover deep analysis and hardware soak
testing.

## Contributing

* Open issues and pull requests at
  <https://github.com/hbmartin/libfreenect2-metal>.
* Keep changes formatted (`.clang-format`) and warning-clean under
  `ENABLE_WARNINGS_AS_ERRORS=ON`.
* Add or update tests under `tests/` for behavior changes, and a guide under
  `doc/` for user-visible features.
* Contributions are accepted under the project's dual
  `Apache-2.0 OR GPL-2.0-only` license. Contributor attributions are collected
  in the `CONTRIB` file; `tools/mkcontrib.py` regenerates it.
* Preserve any additional attribution or redistribution notices already present
  in a file you touch.
