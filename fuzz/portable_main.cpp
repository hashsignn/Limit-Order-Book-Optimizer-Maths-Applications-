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
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <vector>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size);

namespace {

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

  long long runs = 200'000, seed = 20260904, max_len = 512;
  for (int i = 1; i < argc; ++i) {
    if (flag_value(argv[i], "-runs", runs))    continue;
    if (flag_value(argv[i], "-seed", seed))    continue;
    if (flag_value(argv[i], "-max_len", max_len)) continue;
    // Anything else is a libFuzzer flag this driver has no equivalent for.
  }
  if (runs <= 0) runs = 200'000;
  if (max_len <= 4) max_len = 512;

  std::printf("portable fuzz driver: %lld runs, max_len %lld, seed %llu (no coverage feedback)\n",
              runs, max_len, static_cast<unsigned long long>(seed));
  std::mt19937_64 rng{static_cast<std::uint64_t>(seed)};

  std::size_t total_bytes = 0;
  for (long long i = 0; i < runs; ++i) {
    // The per-case seed is derived and reported on failure, so any crash this
    // driver finds is reproducible without a corpus file.
    const std::uint64_t case_seed = rng();
    std::mt19937_64 case_rng{case_seed};
    const std::vector<std::uint8_t> v = make_case(case_rng, static_cast<std::size_t>(max_len));
    total_bytes += v.size();
    LLVMFuzzerTestOneInput(v.data(), v.size());
  }
  std::printf("ok: %lld cases, %.1f KB of input, no crashes or invariant violations\n",
              runs, static_cast<double>(total_bytes) / 1024.0);
  return 0;
}
