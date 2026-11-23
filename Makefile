.PHONY: all build test clean asan tsan bench fuzz

BUILD ?= build

all: build

build:
	cmake -S . -B $(BUILD) -G Ninja -DCMAKE_BUILD_TYPE=Release
	cmake --build $(BUILD) -j

test: build
	ctest --test-dir $(BUILD) --output-on-failure

asan:
	cmake -S . -B build-asan -G Ninja -DCMAKE_BUILD_TYPE=Debug -DTEMPO_SANITIZE=ON
	cmake --build build-asan -j
	ctest --test-dir build-asan --output-on-failure

tsan:
	cmake -S . -B build-tsan -G Ninja -DCMAKE_BUILD_TYPE=Debug -DTEMPO_TSAN=ON
	cmake --build build-tsan -j
	ctest --test-dir build-tsan --output-on-failure

bench: build
	./$(BUILD)/tempo_bench

fuzz: build
	./$(BUILD)/fuzz_record --iterations 20000
	./$(BUILD)/fuzz_json --iterations 20000
	./$(BUILD)/fuzz_http --iterations 20000

clean:
	rm -rf build build-asan build-tsan
