# Building

## Requirements

- CMake ≥ 3.20 and Ninja
- A C++20 compiler (tested: Apple Clang 21 / Xcode 26 on macOS 26, arm64)
- POSIX sockets and standard headers; no third-party libraries, no network
- No CI configuration is checked in; builds are local and reproducible

## Standard build

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Warnings are errors (`-Wall -Wextra -Wpedantic -Werror -Wshadow
-Wnon-virtual-dtor -Woverloaded-virtual -Wunused -Wformat=2`) for the library,
tests and tools. The release build must complete with zero warnings.

## Run the workbench

```sh
./build/tempo_workbench
```

Options:

```
--data-dir PATH       data directory (default: ~/Library/Application Support/Tempo)
--port N              127.0.0.1 port, 0 = ephemeral
--max-memory-mb N     global retained-record memory budget (default 256)
--no-open             do not open the browser
--help
```

The launcher mints a token, starts the server and opens the URL
`http://127.0.0.1:<port>/?token=<token>`. It does not prompt for input. Use
`--no-open` for a headless check:

```sh
./build/tempo_workbench --no-open --data-dir /tmp/tempo-data
```

## Other targets

```sh
./build/tempo_example /tmp/demo.tmpo   # embeddable library demo
./build/tempo_bench                    # micro-benchmarks
./build/fuzz_record --iterations 2000
./build/fuzz_json --iterations 2000
./build/fuzz_http --iterations 2000
make soak                              # (or ctest -R soak) longer loops
make ui-smoke                          # launcher + API + headless DOM check
make install-smoke                     # install + find_package consumer
```

## Sanitizer builds

```sh
make asan   # ASan + UBSan, Debug, runs ctest
make tsan   # TSan, Debug, runs ctest
```

Or configure manually with `-DTEMPO_SANITIZE=ON` / `-DTEMPO_TSAN=ON`.

## CMake options

| Option | Default | Meaning |
|---|---|---|
| `CMAKE_BUILD_TYPE` | `Release` | standard CMake build type |
| `BUILD_TESTING` | `ON` | build and register tests |
| `TEMPO_SANITIZE` | `OFF` | ASan + UBSan for all targets |
| `TEMPO_TSAN` | `OFF` | TSan for all targets |
| `TEMPO_COVERAGE` | `OFF` | GCC/Clang coverage instrumentation |
| `TEMPO_LIBFUZZER` | `OFF` | coverage-guided harnesses (Clang + compiler-rt) |

## Install

```sh
cmake --install build --prefix /usr/local
```

Installs:

- `lib/libtempo.a` and public headers,
- `bin/tempo_workbench`,
- `lib/cmake/tempo/tempoConfig.cmake` (+ version and targets) for
  `find_package(tempo CONFIG REQUIRED)` and `tempo::tempo`,
- `share/doc/tempo/` with README, docs, changelog, limitations and report.

`make install-smoke` installs into a temporary prefix and builds a consumer
project against the package. The example, fuzzers and benchmark remain
build-tree tools.

## Optional libFuzzer / coverage

```sh
cmake -S . -B build-fuzz -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DTEMPO_LIBFUZZER=ON \
  -DCMAKE_C_COMPILER=/opt/homebrew/opt/llvm/bin/clang \
  -DCMAKE_CXX_COMPILER=/opt/homebrew/opt/llvm/bin/clang++
cmake --build build-fuzz --target libfuzzer_record libfuzzer_json
./build-fuzz/libfuzzer_record -max_total_time=30 -max_len=4096
```

Apple Clang does not ship the libFuzzer runtime; CMake fails configuration with a
clear message rather than an obscure link error. The default deterministic
fuzzers need no special toolchain.

Coverage:

```sh
cmake -S . -B build-cov -G Ninja -DCMAKE_BUILD_TYPE=Debug -DTEMPO_COVERAGE=ON
cmake --build build-cov -j
ctest --test-dir build-cov --output-on-failure
```

## Asset embedding

`ui/index.html`, `ui/style.css`, `ui/app.js` and `ui/favicon.svg` are embedded at
build time by `cmake/EmbedAssets.cmake` into
`build/generated/tempo_assets.hpp` as raw string literals. The binary never reads
UI files at runtime. Rebuilding after editing an asset re-runs the embed step.
