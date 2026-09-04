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
  src/order_book.cpp)
target_include_directories(lob PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(lob PUBLIC lob_flags)

find_package(Threads REQUIRED)
target_link_libraries(lob PUBLIC Threads::Threads)

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
  foreach(t types arena histogram tsc journal book book_differential features matching simulator strategies pnl)
    add_executable(test_${t} tests/test_${t}.cpp)
    target_link_libraries(test_${t} PRIVATE lob)
    add_test(NAME ${t} COMMAND test_${t})
  endforeach()

  # The thorough differential pass. Same code, more events — too slow to be the
  # default under a sanitizer, so it is a separate test the release CI runs.
  add_test(NAME book_differential_long COMMAND test_book_differential --long)
  set_tests_properties(book_differential_long PROPERTIES TIMEOUT 600)
endif()

# ---- benchmarks -----------------------------------------------------------
if(LOB_BUILD_BENCH)
  add_executable(bench_measure bench/bench_measure.cpp)
  target_link_libraries(bench_measure PRIVATE lob)

  add_executable(bench_book bench/bench_book.cpp)
  target_link_libraries(bench_book PRIVATE lob)
endif()
