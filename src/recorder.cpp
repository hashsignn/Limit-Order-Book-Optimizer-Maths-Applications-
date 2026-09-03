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
  }
  out += "                                                                        all values in ns\n";
  return out;
}

}  // namespace lob
