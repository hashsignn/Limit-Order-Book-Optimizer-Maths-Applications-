// Minimal assertion harness. Deliberately dependency-free: Phase 0 should build
// and test with nothing but a compiler and CMake.
#pragma once

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace lobtest {

inline int g_failures = 0;
inline int g_checks   = 0;

inline void report(bool ok, const char* expr, const char* file, int line, const std::string& extra) {
  ++g_checks;
  if (ok) return;
  ++g_failures;
  std::fprintf(stderr, "FAIL %s:%d: %s%s%s\n", file, line, expr,
               extra.empty() ? "" : "  --  ", extra.c_str());
}

inline int summary(const char* name) {
  std::printf("%-18s %d checks, %d failures\n", name, g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}

}  // namespace lobtest

#define CHECK(expr) ::lobtest::report((expr), #expr, __FILE__, __LINE__, "")

#define CHECK_EQ(a, b)                                                              \
  do {                                                                              \
    const auto va_ = (a); const auto vb_ = (b);                                     \
    ::lobtest::report(va_ == vb_, #a " == " #b, __FILE__, __LINE__,                  \
                      std::to_string(va_) + " vs " + std::to_string(vb_));          \
  } while (0)

#define CHECK_NEAR(a, b, tol)                                                       \
  do {                                                                              \
    const double va_ = static_cast<double>(a); const double vb_ = static_cast<double>(b); \
    ::lobtest::report(std::fabs(va_ - vb_) <= (tol), #a " ~= " #b, __FILE__, __LINE__, \
                      std::to_string(va_) + " vs " + std::to_string(vb_));          \
  } while (0)

#define CHECK_THROWS(expr)                                                          \
  do {                                                                              \
    bool threw_ = false;                                                            \
    try { (void)(expr); } catch (...) { threw_ = true; }                             \
    ::lobtest::report(threw_, #expr " throws", __FILE__, __LINE__, "");              \
  } while (0)
