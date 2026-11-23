cmake_minimum_required(VERSION 3.20)

project(tempo
  VERSION 0.1.0
  DESCRIPTION "Deterministic record/replay workbench for cooperative C++ tasks"
  LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(TEMPO_SANITIZE "Build with address/undefined sanitizers" OFF)
option(TEMPO_TSAN "Build with thread sanitizer" OFF)
option(TEMPO_BUILD_TOOLS "Build demo, fuzz and benchmark tools" ON)

set(TEMPO_WARNINGS
  -Wall
  -Wextra
  -Wpedantic
  -Werror
  -Wshadow
  -Wnon-virtual-dtor
  -Woverloaded-virtual
  -Wunused
  -Wformat=2)

find_package(Threads REQUIRED)

add_library(tempo STATIC
  src/version.cpp
  src/runtime/task.cpp
  src/runtime/context.cpp
  src/runtime/scheduler.cpp
  src/virtual/rng.cpp
  src/virtual/clock.cpp
  src/capability/system_capability.cpp
  src/record/event.cpp
  src/record/crc32.cpp
  src/record/string_table.cpp
  src/record/record.cpp
  src/record/record_io.cpp
  src/record/record_run.cpp
  src/replay/replay.cpp
  src/replay/diff.cpp
)
target_include_directories(tempo PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)
target_include_directories(tempo PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${CMAKE_CURRENT_BINARY_DIR}/generated)
target_compile_options(tempo PRIVATE ${TEMPO_WARNINGS})
target_compile_features(tempo PUBLIC cxx_std_20)
target_link_libraries(tempo PUBLIC Threads::Threads)

if(TEMPO_SANITIZE)
  target_compile_options(tempo PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer)
  target_link_options(tempo PUBLIC -fsanitize=address,undefined)
endif()
if(TEMPO_TSAN)
  target_compile_options(tempo PUBLIC -fsanitize=thread -fno-omit-frame-pointer)
  target_link_options(tempo PUBLIC -fsanitize=thread)
endif()

include(CTest)
enable_testing()

function(tempo_add_test name)
  add_executable(${name} tests/${name}.cpp)
  target_link_libraries(${name} PRIVATE tempo)
  target_compile_options(${name} PRIVATE ${TEMPO_WARNINGS})
  target_include_directories(${name} PRIVATE tests)
  add_test(NAME ${name} COMMAND ${name})
  set_tests_properties(${name} PROPERTIES TIMEOUT 180)
endfunction()

if(BUILD_TESTING)
  tempo_add_test(version_test)
  tempo_add_test(rng_test)
  tempo_add_test(clock_test)
  tempo_add_test(crc32_test)
  tempo_add_test(string_table_test)
  tempo_add_test(event_test)
  tempo_add_test(record_roundtrip_test)
  tempo_add_test(record_recovery_test)
  tempo_add_test(record_fuzz_test)
  tempo_add_test(record_run_test)
  tempo_add_test(replay_test)
  tempo_add_test(mutation_test)
  tempo_add_test(diff_test)
  tempo_add_test(task_test)
  tempo_add_test(scheduler_test)
  tempo_add_test(cancellation_test)
  tempo_add_test(capability_test)
  tempo_add_test(system_capability_test)
  tempo_add_test(socket_capability_test)
endif()

install(TARGETS tempo ARCHIVE DESTINATION lib)
install(DIRECTORY include/ DESTINATION include)
