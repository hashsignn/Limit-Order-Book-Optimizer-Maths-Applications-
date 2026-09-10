#include "lob/measure/recorder.hpp"

#include <cstdio>

namespace lob {

std::string LatencyRecorder::report() const {
  std::string out =
      "stage                      count      p50      p90      p99    p99.9   p99.99      max\n"
      "----------------------------------------------------------------------------------------\n";
  char line[256];
  for (std::size_t i = 0; i < kStageCount; ++i) {
    const Histogram& h = hists_[i];
    if (h.empty()) continue;
    std::snprintf(line, sizeof(line),
                  "%-22s %9lld %8lld %8lld %8lld %8lld %8lld %8lld\n",
                  std::string(stage_name(static_cast<Stage>(i))).c_str(),
                  static_cast<long long>(h.count()),
                  static_cast<long long>(h.value_at_percentile(50.0)),
                  static_cast<long long>(h.value_at_percentile(90.0)),
                  static_cast<long long>(h.value_at_percentile(99.0)),
                  static_cast<long long>(h.value_at_percentile(99.9)),
                  static_cast<long long>(h.value_at_percentile(99.99)),
                  static_cast<long long>(h.max()));
    out += line;
    // A clamped maximum, reported as if it were the maximum, is the one failure
    // this layer must not have: the whole argument for a histogram over a mean
    // is that it is honest about the tail. Histogram::summary() has always said
    // so and this table never did.
    if (h.overflow_count() > 0) {
      std::snprintf(line, sizeof(line),
                    "%-22s %9s   %lld sample(s) OVER THE %lld ns CEILING; true max %lld ns\n",
                    "", "!!", static_cast<long long>(h.overflow_count()),
                    static_cast<long long>(h.value_at_percentile(100.0)),
                    static_cast<long long>(h.true_max()));
      out += line;
    }
  }
  out += "                                                                        all values in ns\n";
  return out;
}

}  // namespace lob
