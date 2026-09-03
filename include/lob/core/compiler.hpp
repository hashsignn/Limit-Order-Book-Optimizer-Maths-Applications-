// Compiler-specific helpers, isolated here so the rest of the code stays clean.
#pragma once

#include <cstdint>

namespace lob {

// Stops the optimiser deleting work whose result is otherwise unused. Needed
// in benchmarks and in any measured loop, where dead-code elimination would
// otherwise make the thing you are timing disappear.
template <typename T>
[[gnu::always_inline]] inline void do_not_optimize(T&& value) noexcept {
#if defined(__GNUC__) || defined(__clang__)
  asm volatile("" : : "r,m"(value) : "memory");
#else
  volatile auto sink = value;
  (void)sink;
#endif
}

// Forces pending writes to be visible before the next timed region.
[[gnu::always_inline]] inline void clobber_memory() noexcept {
#if defined(__GNUC__) || defined(__clang__)
  asm volatile("" : : : "memory");
#endif
}

// 128-bit unsigned, where the compiler has it. __extension__ keeps -Wpedantic
// quiet about a type ISO C++ does not define.
#if defined(__SIZEOF_INT128__)
__extension__ using u128 = unsigned __int128;
#define LOB_HAS_INT128 1
#else
#define LOB_HAS_INT128 0
#endif

}  // namespace lob
