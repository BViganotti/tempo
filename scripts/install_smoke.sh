#!/bin/sh
# Install Tempo to a temporary prefix and build a consumer with find_package.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
BUILD=${BUILD:-$ROOT/build}
PREFIX=$(mktemp -d "${TMPDIR:-/tmp}/tempo-install-smoke.XXXXXX")
CONSUMER=$(mktemp -d "${TMPDIR:-/tmp}/tempo-consumer.XXXXXX")
cleanup() { rm -rf "$PREFIX" "$CONSUMER"; }
trap cleanup EXIT INT TERM

cmake --install "$BUILD" --prefix "$PREFIX" >/dev/null
test -x "$PREFIX/bin/tempo_workbench"
test -f "$PREFIX/lib/cmake/tempo/tempoConfig.cmake"
test -f "$PREFIX/include/tempo.h"

cat >"$CONSUMER/CMakeLists.txt" <<'CMAKE'
cmake_minimum_required(VERSION 3.20)
project(tempo_consumer CXX)
set(CMAKE_CXX_STANDARD 20)
find_package(tempo 0.2 REQUIRED)
add_executable(consumer main.cpp)
target_link_libraries(consumer PRIVATE tempo::tempo)
CMAKE
cat >"$CONSUMER/main.cpp" <<'CPP'
#include "tempo.h"
#include <cstdio>
int main() { std::printf("%s\n", std::string(tempo::version_banner()).c_str()); return 0; }
CPP

cmake -S "$CONSUMER" -B "$CONSUMER/build" -G Ninja \
  -DCMAKE_PREFIX_PATH="$PREFIX" >/dev/null
cmake --build "$CONSUMER/build" >/dev/null
OUTPUT=$("$CONSUMER/build/consumer")
printf '%s' "$OUTPUT" | grep -q 'Tempo 0.2.0' || {
  echo "install_smoke: consumer output did not contain the version banner" >&2
  exit 1
}
echo "install_smoke: OK ($OUTPUT)"
