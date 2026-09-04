cmake_minimum_required(VERSION 3.20)
project(lob VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

option(LOB_BUILD_TESTS "Build tests"      ON)
option(LOB_BUILD_BENCH "Build benchmarks" ON)
option(LOB_NATIVE      "Build with -march=native" ON)
option(LOB_WERROR      "Treat warnings as errors"  ON)
option(LOB_BUILD_FUZZ  "Build fuzz targets"        ON)

add_library(lob_flags INTERFACE)
target_compile_features(lob_flags INTERFACE cxx_std_20)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(lob_flags INTERFACE
    -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion
    -Wnon-virtual-dtor -Wold-style-cast -Wcast-align -Wunused
    -Woverloaded-virtual -Wdouble-promotion -Wformat=2
    # Determinism: no FMA contraction, no reassociation. A latency number that
    # changes with the optimiser is not a measurement.
    -ffp-contract=off
    -fno-omit-frame-pointer          # keep profiles readable
  )
  if(LOB_WERROR)
    target_compile_options(lob_flags INTERFACE -Werror)
  endif()
  if(LOB_NATIVE)
    include(CheckCXXCompilerFlag)
    check_cxx_compiler_flag(-march=native LOB_HAS_MARCH_NATIVE)
    if(LOB_HAS_MARCH_NATIVE)
      target_compile_options(lob_flags INTERFACE $<$<CONFIG:Release,RelWithDebInfo>:-march=native>)
    endif()
  endif()
endif()

# ---- the library ----------------------------------------------------------
add_library(lob STATIC
  src/tsc.cpp src/histogram.cpp src/journal.cpp src/recorder.cpp
  src/order_book.cpp src/bitstamp.cpp src/line_reader.cpp)
target_include_directories(lob PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(lob PUBLIC lob_flags)

find_package(Threads REQUIRED)
target_link_libraries(lob PUBLIC Threads::Threads)

# Optional: read gzipped captures directly. Without it the replay tool asks the
# user to pipe gunzip, which works everywhere, so this is convenience and never
# a requirement.
find_package(ZLIB QUIET)
if(ZLIB_FOUND)
  target_link_libraries(lob PUBLIC ZLIB::ZLIB)
  target_compile_definitions(lob PUBLIC LOB_HAVE_ZLIB=1)
  message(STATUS "zlib found: gzipped captures readable directly")
else()
  message(STATUS "zlib not found: gzipped captures need 'gunzip -c file.gz | replay --bitstamp -'")
endif()

# ---- apps -----------------------------------------------------------------
add_executable(latency_demo apps/latency_demo/main.cpp)
target_link_libraries(latency_demo PRIVATE lob)

add_executable(jitter_probe apps/jitter_probe/main.cpp)
target_link_libraries(jitter_probe PRIVATE lob)

add_executable(replay apps/replay/main.cpp)
target_link_libraries(replay PRIVATE lob)

add_executable(sim_demo apps/sim_demo/main.cpp)
target_link_libraries(sim_demo PRIVATE lob)

add_executable(backtest apps/backtest/main.cpp)
target_link_libraries(backtest PRIVATE lob)

# ---- tests ----------------------------------------------------------------
if(LOB_BUILD_TESTS)
  enable_testing()

  # A committed slice of a real venue capture, if one is present. Synthetic data
  # can only ever prove the decoder agrees with the generator that produced it;
  # this is the test that the decoder agrees with an exchange.
  file(GLOB LOB_SAMPLE_CAPTURES "${CMAKE_CURRENT_SOURCE_DIR}/data/samples/*_bitstamp.jsonl*")
  list(SORT LOB_SAMPLE_CAPTURES)
  foreach(t types arena histogram tsc journal book book_differential features matching simulator strategies pnl order_map properties bitstamp)
    add_executable(test_${t} tests/test_${t}.cpp)
    target_link_libraries(test_${t} PRIVATE lob)
    add_test(NAME ${t} COMMAND test_${t})
  endforeach()

  # One regression test per committed capture. Each asserts the same thing:
  # the book survived a real exchange session with zero invariant violations,
  # over a capture whose event_id chain is provably unbroken.
  foreach(cap ${LOB_SAMPLE_CAPTURES})
    string(REGEX REPLACE "_bitstamp\\.jsonl(\\.gz)?$" "_snapshot.json" snap "${cap}")
    if(EXISTS "${snap}")
      get_filename_component(capname "${cap}" NAME)
      string(REGEX REPLACE "_bitstamp\\.jsonl(\\.gz)?$" "" capname "${capname}")
      add_test(NAME capture_${capname} COMMAND test_bitstamp "${cap}" "${snap}")
      set_tests_properties(capture_${capname} PROPERTIES TIMEOUT 300)
      message(STATUS "real-capture test: ${capname}")
    else()
      message(WARNING "capture ${cap} has no matching _snapshot.json; skipped")
    endif()
  endforeach()
  if(NOT LOB_SAMPLE_CAPTURES)
    message(STATUS "no data/samples captures: real-data tests skipped")
  endif()

  # The thorough differential pass. Same code, more events — too slow to be the
  # default under a sanitizer, so it is a separate test the release CI runs.
  add_test(NAME book_differential_long COMMAND test_book_differential --long)
  set_tests_properties(book_differential_long PROPERTIES TIMEOUT 600)
endif()

# ---- fuzz targets ---------------------------------------------------------
# Built two ways. With libFuzzer where its runtime exists: coverage-guided, with
# corpus minimisation and crash reproduction. Everywhere else, linked against a
# portable driver so the targets still run on every commit — blind brute force
# is a poor substitute for coverage feedback, but it is a far better one than a
# target that never executes.
if(LOB_BUILD_FUZZ)
  include(CheckCXXSourceCompiles)
  # No main() in the probe: libFuzzer supplies its own, and defining one here
  # makes the link fail for a reason that has nothing to do with availability.
  set(CMAKE_REQUIRED_FLAGS "-fsanitize=fuzzer")
  set(CMAKE_REQUIRED_LINK_OPTIONS "-fsanitize=fuzzer")
  check_cxx_source_compiles(
    "#include <cstdint>\n#include <cstddef>\nextern \"C\" int LLVMFuzzerTestOneInput(const uint8_t*, size_t){return 0;}"
    LOB_HAS_LIBFUZZER)
  unset(CMAKE_REQUIRED_FLAGS)
  unset(CMAKE_REQUIRED_LINK_OPTIONS)

  foreach(f book matching bitstamp)
    if(LOB_HAS_LIBFUZZER)
      add_executable(fuzz_${f} fuzz/fuzz_${f}.cpp)
      target_compile_options(fuzz_${f} PRIVATE -fsanitize=fuzzer,address,undefined)
      target_link_options(fuzz_${f}    PRIVATE -fsanitize=fuzzer,address,undefined)
    else()
      add_executable(fuzz_${f} fuzz/fuzz_${f}.cpp fuzz/portable_main.cpp)
    endif()
    target_include_directories(fuzz_${f} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(fuzz_${f} PRIVATE lob)
  endforeach()

  if(LOB_BUILD_TESTS)
    # A short pass runs as a normal test, so fuzzing gates every commit rather
    # than being something someone remembers to do occasionally.
    add_test(NAME fuzz_book_short     COMMAND fuzz_book     -runs=20000)
    add_test(NAME fuzz_matching_short COMMAND fuzz_matching -runs=20000)
    add_test(NAME fuzz_bitstamp_short COMMAND fuzz_bitstamp -runs=20000)
    set_tests_properties(fuzz_book_short fuzz_matching_short fuzz_bitstamp_short
                         PROPERTIES TIMEOUT 300)
  endif()
endif()

# ---- benchmarks -----------------------------------------------------------
if(LOB_BUILD_BENCH)
  add_executable(bench_measure bench/bench_measure.cpp)
  target_link_libraries(bench_measure PRIVATE lob)

  add_executable(bench_book bench/bench_book.cpp)
  target_link_libraries(bench_book PRIVATE lob)
endif()
