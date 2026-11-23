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
--data-dir PATH   data directory (default: ~/Library/Application Support/Tempo)
--port N          127.0.0.1 port, 0 = ephemeral
--no-open         do not open the browser
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

## Install

```sh
cmake --install build --prefix /usr/local
```

Installs the static library and public headers. The launcher, example, fuzzers
and benchmark are build-tree tools; copy `tempo_workbench` if you want it on
`PATH`.

## Asset embedding

`ui/index.html`, `ui/style.css`, `ui/app.js` and `ui/favicon.svg` are embedded at
build time by `cmake/EmbedAssets.cmake` into
`build/generated/tempo_assets.hpp` as raw string literals. The binary never reads
UI files at runtime. Rebuilding after editing an asset re-runs the embed step.
