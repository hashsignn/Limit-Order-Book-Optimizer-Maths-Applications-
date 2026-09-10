// Portable fuzz driver.
//
// libFuzzer needs a compiler runtime that is not present everywhere — not on
// this project's CI image by default, and not on a typical Windows/WSL box. A
// fuzz target that only runs where the toolchain cooperates is a fuzz target
// that never runs.
//
// So the targets are written against the standard LLVMFuzzerTestOneInput
// signature and can be driven two ways: by real libFuzzer where it exists
// (coverage-guided, with corpus minimisation and crash reproduction), or by
// this driver, which needs nothing.
//
// This is NOT as good as libFuzzer: it is blind, with no coverage feedback, so
// it explores by brute force rather than by learning which inputs are
// interesting. It is a lower bound, not a substitute. What it buys is that the
// targets run everywhere, on every commit, instead of only where someone
// remembered to install a runtime.
#include <atomic>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <vector>

#include <unistd.h>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size);

namespace {

// The case in flight, so the handler below can name it. Written before each
// case and read from a signal handler, hence the atomic; only async-signal-safe
// calls are made from the handler itself.
std::atomic<std::uint64_t> g_case_seed{0};
long long g_case_index = -1;
long long g_max_len    = 512;

extern "C" void crash_handler(int sig) {
  char buf[160];
  const int n = std::snprintf(buf, sizeof buf,
      "\n*** signal %d on case %lld ***\nreproduce with:  -replay_seed=%llu -max_len=%lld\n",
      sig, g_case_index,
      static_cast<unsigned long long>(g_case_seed.load(std::memory_order_relaxed)),
      g_max_len);
  if (n > 0) { const ssize_t w = ::write(2, buf, static_cast<std::size_t>(n)); (void)w; }
  std::signal(sig, SIG_DFL);
  std::raise(sig);                 // die the way we would have, so exit codes hold
}

void install_crash_handler() {
  for (const int sig : {SIGSEGV, SIGABRT, SIGILL, SIGBUS, SIGFPE})
    std::signal(sig, crash_handler);
}

// A crash under this driver must be reproducible. Every case is generated from
// a seed that is printed, so a failure can be replayed exactly.
std::vector<std::uint8_t> make_case(std::mt19937_64& rng, std::size_t max_len) {
  const std::size_t len = 4 + (rng() % max_len);
  std::vector<std::uint8_t> v(len);

  // Three shapes, because pure random bytes rarely produce long runs of the
  // same operation, and long runs are where state accumulates.
  switch (rng() % 3) {
    case 0:   // uniform noise
      for (auto& b : v) b = static_cast<std::uint8_t>(rng());
      break;
    case 1: { // low-entropy: few distinct bytes, so ids and prices collide hard
      const std::uint8_t alphabet[4] = {
          static_cast<std::uint8_t>(rng()), static_cast<std::uint8_t>(rng()),
          static_cast<std::uint8_t>(rng()), static_cast<std::uint8_t>(rng())};
      for (auto& b : v) b = alphabet[rng() % 4];
      break;
    }
    default: { // repeated short motif, to build deep state from one operation
      const std::size_t motif = 1 + (rng() % 8);
      std::vector<std::uint8_t> m(motif);
      for (auto& b : m) b = static_cast<std::uint8_t>(rng());
      for (std::size_t i = 0; i < len; ++i) v[i] = m[i % motif];
      break;
    }
  }
  return v;
}

}  // namespace

namespace {

// libFuzzer's command line is `-flag=value`, and the same invocation has to
// work under either driver — otherwise CI runs one syntax and a developer runs
// the other, and the fuzz targets quietly stop being exercised somewhere.
// Unknown libFuzzer flags are accepted and ignored rather than rejected.
bool flag_value(const char* arg, const char* name, long long& out) {
  const std::size_t n = std::strlen(name);
  if (std::strncmp(arg, name, n) != 0 || arg[n] != '=') return false;
  out = std::strtoll(arg + n + 1, nullptr, 10);
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  // Replay mode: any non-flag argument is a crash file to reproduce.
  if (argc > 1 && argv[1][0] != '-') {
    std::FILE* f = std::fopen(argv[1], "rb");
    if (f == nullptr) { std::fprintf(stderr, "cannot open %s\n", argv[1]); return 1; }
    std::vector<std::uint8_t> buf;
    std::uint8_t chunk[4096];
    std::size_t n;
    while ((n = std::fread(chunk, 1, sizeof(chunk), f)) > 0) buf.insert(buf.end(), chunk, chunk + n);
    std::fclose(f);
    std::printf("replaying %s (%zu bytes)\n", argv[1], buf.size());
    LLVMFuzzerTestOneInput(buf.data(), buf.size());
    std::printf("no crash\n");
    return 0;
  }

  long long runs = 200'000, seed = 20260904, max_len = 512, replay_seed = 0;
  bool has_replay = false;
  for (int i = 1; i < argc; ++i) {
    if (flag_value(argv[i], "-runs", runs))    continue;
    if (flag_value(argv[i], "-seed", seed))    continue;
    if (flag_value(argv[i], "-max_len", max_len)) continue;
    // Replay exactly the case a printed seed names, which is what makes the
    // line above a reproducer rather than a note.
    if (flag_value(argv[i], "-replay_seed", replay_seed)) { has_replay = true; continue; }
    // Anything else is a libFuzzer flag this driver has no equivalent for.
  }
  if (runs <= 0) runs = 200'000;
  if (max_len <= 4) max_len = 512;

  if (has_replay) {
    std::mt19937_64 case_rng{static_cast<std::uint64_t>(replay_seed)};
    const std::vector<std::uint8_t> v = make_case(case_rng, static_cast<std::size_t>(max_len));
    std::printf("replaying seed %llu (%zu bytes, max_len %lld)\n",
                static_cast<unsigned long long>(replay_seed), v.size(), max_len);
    LLVMFuzzerTestOneInput(v.data(), v.size());
    std::printf("no crash\n");
    return 0;
  }

  std::printf("portable fuzz driver: %lld runs, max_len %lld, seed %llu (no coverage feedback)\n",
              runs, max_len, static_cast<unsigned long long>(seed));
  std::mt19937_64 rng{static_cast<std::uint64_t>(seed)};

  // The current seed is published to a handler that runs when the target dies,
  // so the reproducer survives the crash rather than dying with it.
  //
  // This file used to claim the seed was "reported on failure" and never print
  // it anywhere. A target that hits __builtin_trap() kills the process, so a
  // crash found on CI could not be replayed locally -- which removes most of
  // the value of finding one. Printing every case instead would put 200,000
  // lines in a CI log, so it is printed exactly once, when it matters.
  g_max_len = max_len;
  install_crash_handler();

  std::size_t total_bytes = 0;
  for (long long i = 0; i < runs; ++i) {
    const std::uint64_t case_seed = rng();
    g_case_index = i;
    g_case_seed.store(case_seed, std::memory_order_relaxed);
    std::mt19937_64 case_rng{case_seed};
    const std::vector<std::uint8_t> v = make_case(case_rng, static_cast<std::size_t>(max_len));
    total_bytes += v.size();
    LLVMFuzzerTestOneInput(v.data(), v.size());
  }
  std::printf("ok: %lld cases, %.1f KB of input, no crashes or invariant violations\n",
              runs, static_cast<double>(total_bytes) / 1024.0);
  return 0;
}
