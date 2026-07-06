# Self-hosted macOS CI runner setup {#self_hosted_runner}

The CI workflow (`.github/workflows/ci.yml`) runs on a **self-hosted macOS
runner** rather than a GitHub-hosted one, because two of its jobs need a real
Apple GPU:

- **build-test** builds with Metal enabled and runs the full test suite,
  including the Metal-vs-CPU depth parity test on the GPU.
- **sanitizers** rebuilds the hardware-free tests under ASan + UBSan.
- **format** / **static-analysis** run `clang-format` / `clang-tidy` (advisory).

This guide sets up a machine to serve that workflow. No Kinect hardware is
required — every test is hardware-free; only a Metal-capable GPU is used.

## 1. Machine requirements

- A Mac (Apple Silicon or Intel) running a current macOS.
- A **real GPU and a logged-in graphics session** — the Metal parity test
  creates an `MTLDevice`. A headless daemon with no window-server session will
  make that test `SKIP` rather than run, so prefer running the runner inside a
  logged-in user session (see [step 4](#run-as-a-service)).
- Outbound network access — the test build fetches GoogleTest via CMake
  `FetchContent` at configure time.
- A few GB of free disk for the toolchains, Homebrew packages, and per-job
  build directories.

## 2. Install prerequisites

### Xcode + the Metal toolchain

Install **full Xcode** (not just the Command Line Tools) — the Metal shader
compiler ships only with Xcode. Then verify the compiler actually runs:

```bash
sudo xcode-select -s /Applications/Xcode.app/Contents/Developer
xcrun -sdk macosx metal --version
```

Supported across recent macOS releases, including **macOS 15 Sequoia**
(Xcode 16) and **macOS 26 Tahoe** (Xcode 26):

- **macOS 15 Sequoia / Xcode 16** — the Metal toolchain is bundled with Xcode.
  `xcrun -sdk macosx metal --version` works out of the box; nothing extra to do.
- **macOS 26 Tahoe / Xcode 26 and later** — the `metal` launcher stub is present
  but cannot execute until the Metal Toolchain component is downloaded once:

  ```bash
  xcodebuild -downloadComponent MetalToolchain
  ```

If `metal --version` fails, the build still succeeds but Metal support is
disabled and the parity test skips — so fix this before relying on CI.

### Build dependencies (Homebrew)

```bash
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"  # if needed
brew install cmake pkg-config libusb glfw3 jpeg-turbo
```

Use the Homebrew prefix that matches the machine: `/opt/homebrew` on Apple
Silicon, `/usr/local` on Intel. Do not run the shell/CMake under Rosetta on
Apple Silicon (see the macOS notes in the top-level `README.md`).

### Advisory-job tools (optional but recommended)

```bash
brew install clang-format llvm   # llvm provides clang-tidy
```

The `format` and `static-analysis` jobs are advisory (`continue-on-error`) and
**skip gracefully with a notice** if these are absent — installing them just
turns the checks on. The workflow finds `clang-tidy` on `PATH` or falls back to
`/opt/homebrew/opt/llvm/bin/clang-tidy`.

## 3. Register the runner with GitHub

In the repository: **Settings → Actions → Runners → New self-hosted runner →
macOS**. GitHub shows the exact download/configure commands with a
registration token. They look like:

```bash
mkdir -p ~/actions-runner && cd ~/actions-runner
curl -o actions-runner-osx.tar.gz -L <url-from-github>
tar xzf actions-runner-osx.tar.gz

./config.sh --url https://github.com/<owner>/<repo> --token <token>
```

During `config.sh`, when prompted for **labels**, keep the defaults and make
sure the runner ends up with **both** `self-hosted` and `macOS` labels — the
workflow targets `runs-on: [self-hosted, macOS]`, so a runner missing either
label will never be picked. (`self-hosted` is added automatically; `macOS` is a
default label for the macOS runner package.)

## 4. Run the runner {#run-as-a-service}

For occasional/interactive use:

```bash
cd ~/actions-runner
./run.sh
```

To keep it running across reboots, install it as a launchd service:

```bash
./svc.sh install
./svc.sh start
./svc.sh status
```

> **Metal note:** the service must run in a context that has access to the
> window server, or the Metal parity test will skip. Running `./run.sh` (or the
> service) from inside a logged-in desktop session is the simplest way to
> guarantee GPU access. If you rely on `svc.sh`, log in as the runner's user and
> confirm the parity test *runs* (not skips) on the first CI run — see the
> verification below.

## 5. Verify the setup

Either push a branch and watch the CI jobs, or reproduce them locally on the
runner:

```bash
# build-test job
cmake -B build -DBUILD_TESTING=ON -DENABLE_METAL=ON -DBUILD_EXAMPLES=ON
cmake --build build -j
ctest --test-dir build --output-on-failure

# sanitizers job
cmake -B build-asan -DBUILD_TESTING=ON -DENABLE_SANITIZERS=ON -DENABLE_METAL=OFF -DBUILD_EXAMPLES=OFF
cmake --build build-asan -j
ctest --test-dir build-asan -LE gpu --output-on-failure

# Confirm the Metal parity test actually RAN (not skipped):
build/bin/unit_tests --gtest_filter='MetalCpuParity.*'
# Expect: "using device <your GPU>" and [  PASSED  ], not [  SKIPPED  ].
```

A healthy runner produces a green `build-test` (with the parity cases running
on the GPU) and a clean `sanitizers` run.

## 6. Maintenance & security notes

- **Trust boundary.** A self-hosted runner executes any workflow triggered
  against the repo. Keep it dedicated to this repository and restrict who can
  open PRs that run CI; treat the machine as compromisable by workflow code.
- **Clean checkouts.** The workflow checks out with `clean: true` and each job
  uses its own build directory (`build`, `build-asan`, `build-tidy`) to avoid
  cross-contamination on the persistent machine. Occasionally prune the
  `~/actions-runner/_work` tree if disk fills up.
- **Toolchain drift.** After a macOS or Xcode update, re-run
  `xcrun -sdk macosx metal --version` (and, on Xcode 26+, re-download the Metal
  Toolchain) so the parity test keeps running instead of silently skipping.
- **Updating the runner.** The GitHub runner agent self-updates; if it falls
  too far behind, re-run `config.sh remove --token <token>` and re-register.
