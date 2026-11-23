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
  src/debug/debug.cpp
  src/ui/json.cpp
  src/ui/format.cpp
  src/ui/http.cpp
  src/ui/assets.cpp
  src/ui/workbench.cpp
  src/scenario/registry.cpp
  src/scenario/demo.cpp
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

# ---------------------------------------------------------------------------
# UI embedding: original HTML/CSS/JS is compiled into the binary.
# ---------------------------------------------------------------------------
set(TEMPO_UI_FILES
  ${CMAKE_CURRENT_SOURCE_DIR}/ui/index.html
  ${CMAKE_CURRENT_SOURCE_DIR}/ui/style.css
  ${CMAKE_CURRENT_SOURCE_DIR}/ui/app.js
  ${CMAKE_CURRENT_SOURCE_DIR}/ui/favicon.svg)
set(TEMPO_UI_HEADER ${CMAKE_CURRENT_BINARY_DIR}/generated/tempo_assets.hpp)
string(REPLACE ";" "," TEMPO_UI_FILES_ARG "${TEMPO_UI_FILES}")
add_custom_command(
  OUTPUT ${TEMPO_UI_HEADER}
  COMMAND ${CMAKE_COMMAND}
    -DOUT=${TEMPO_UI_HEADER}
    -DROOT=${CMAKE_CURRENT_SOURCE_DIR}
    "-DFILES=${TEMPO_UI_FILES_ARG}"
    -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbedAssets.cmake
  DEPENDS ${TEMPO_UI_FILES} ${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbedAssets.cmake
  COMMENT "Embedding Tempo UI assets")
add_custom_target(tempo_assets DEPENDS ${TEMPO_UI_HEADER})
add_dependencies(tempo tempo_assets)

# Scenario build fingerprint: records stamp it, replay warns on mismatch.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  ${CMAKE_CURRENT_SOURCE_DIR}/src/scenario/demo.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/scenario/registry.cpp)
file(SHA256 ${CMAKE_CURRENT_SOURCE_DIR}/src/scenario/demo.cpp _tempo_demo_sha)
file(SHA256 ${CMAKE_CURRENT_SOURCE_DIR}/src/scenario/registry.cpp _tempo_registry_sha)
string(SHA256 TEMPO_SCENARIO_BUILD_ID "${_tempo_demo_sha};${_tempo_registry_sha}")
configure_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/cmake/tempo_build_id.hpp.in
  ${CMAKE_CURRENT_BINARY_DIR}/generated/tempo_build_id.hpp
  @ONLY)

include(CTest)
enable_testing()

add_executable(tempo_workbench app/main.cpp)
target_link_libraries(tempo_workbench PRIVATE tempo)
target_compile_options(tempo_workbench PRIVATE ${TEMPO_WARNINGS})

add_executable(tempo_example examples/tempo_demo/main.cpp)
target_link_libraries(tempo_example PRIVATE tempo)
target_compile_options(tempo_example PRIVATE ${TEMPO_WARNINGS})

add_executable(tempo_bench bench/bench.cpp)
target_link_libraries(tempo_bench PRIVATE tempo)
target_compile_options(tempo_bench PRIVATE ${TEMPO_WARNINGS})

foreach(_fuzzer fuzz_record fuzz_json fuzz_http)
  add_executable(${_fuzzer} fuzz/${_fuzzer}.cpp)
  target_link_libraries(${_fuzzer} PRIVATE tempo)
  target_compile_options(${_fuzzer} PRIVATE ${TEMPO_WARNINGS})
  if(BUILD_TESTING)
    add_test(NAME ${_fuzzer}_smoke COMMAND ${_fuzzer} --iterations 500)
    set_tests_properties(${_fuzzer}_smoke PROPERTIES TIMEOUT 120)
  endif()
endforeach()

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
  tempo_add_test(format_test)
  tempo_add_test(locale_test)
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
  tempo_add_test(debug_test)
  tempo_add_test(breakpoint_test)
  tempo_add_test(json_test)
  tempo_add_test(http_test)
  tempo_add_test(scenario_test)
  tempo_add_test(workbench_test)
  tempo_add_test(limits_test)
  tempo_add_test(property_test)
  tempo_add_test(record_property_test)
  tempo_add_test(replay_capability_test)
  tempo_add_test(workbench_recovery_test)
  tempo_add_test(fs_permissions_test)
  tempo_add_test(workbench_memory_test)
  tempo_add_test(workbench_archive_test)
  tempo_add_test(build_id_test)
  tempo_add_test(async_job_test)
  tempo_add_test(json_property_test)
  tempo_add_test(diff_property_test)
  tempo_add_test(http_property_test)
  tempo_add_test(integration_test)
  tempo_add_test(task_test)
  tempo_add_test(scheduler_test)
  tempo_add_test(cancellation_test)
  tempo_add_test(capability_test)
  tempo_add_test(system_capability_test)
  tempo_add_test(socket_capability_test)
endif()

include(CMakePackageConfigHelpers)

install(TARGETS tempo EXPORT tempoTargets ARCHIVE DESTINATION lib)
install(TARGETS tempo_workbench RUNTIME DESTINATION bin)
install(DIRECTORY include/ DESTINATION include)
install(FILES LICENSE README.md CHANGELOG.md LIMITATIONS.md FINAL_REPORT.md
  DESTINATION share/doc/tempo)
install(DIRECTORY docs/ DESTINATION share/doc/tempo/docs)
install(EXPORT tempoTargets
  NAMESPACE tempo::
  FILE tempoTargets.cmake
  DESTINATION lib/cmake/tempo)
configure_package_config_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/cmake/tempoConfig.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/tempoConfig.cmake
  INSTALL_DESTINATION lib/cmake/tempo)
write_basic_package_version_file(
  ${CMAKE_CURRENT_BINARY_DIR}/tempoConfigVersion.cmake
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY SameMajorVersion)
install(FILES
  ${CMAKE_CURRENT_BINARY_DIR}/tempoConfig.cmake
  ${CMAKE_CURRENT_BINARY_DIR}/tempoConfigVersion.cmake
  DESTINATION lib/cmake/tempo)
