// Per-stage latency recording.
//
// One histogram per pipeline stage, so a tick-to-trade number can be attributed
// rather than just observed. A single end-to-end figure tells you that you are
// slow; the per-stage breakdown tells you where.
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "lob/measure/histogram.hpp"
#include "lob/measure/tsc.hpp"

namespace lob {

// The stages of the inbound path, matching docs/03-metrics-and-estimators.md §2.
enum class Stage : std::uint8_t {
  WireToUserspace = 0,
  Decode,
  BookUpdate,
  FeatureCompute,
  PolicyEval,
  GatewayEncode,
  TickToTrade,
  Count
};

[[nodiscard]] constexpr std::string_view stage_name(Stage s) noexcept {
  switch (s) {
    case Stage::WireToUserspace: return "wire_to_userspace";
    case Stage::Decode:          return "decode";
    case Stage::BookUpdate:      return "book_update";
    case Stage::FeatureCompute:  return "feature_compute";
    case Stage::PolicyEval:      return "policy_eval";
    case Stage::GatewayEncode:   return "gateway_encode";
    case Stage::TickToTrade:     return "tick_to_trade";
    case Stage::Count:           return "?";
  }
  return "?";
}

inline constexpr std::size_t kStageCount = static_cast<std::size_t>(Stage::Count);

// Single-threaded by design: one recorder per pinned thread, merged at report
// time. Sharing one across threads would need atomics on the record path, which
// is exactly the cost we are trying to measure.
class LatencyRecorder {
 public:
  // `highest_ns` bounds the histogram range; the default 1 s is far above any
  // plausible stage time, so anything clamped is a genuine anomaly worth seeing.
  explicit LatencyRecorder(std::int64_t highest_ns = 1'000'000'000LL,
                           int significant_digits  = 3) {
    hists_.reserve(kStageCount);
    for (std::size_t i = 0; i < kStageCount; ++i) hists_.emplace_back(highest_ns, significant_digits);
  }

  void record_nanos(Stage s, Nanos ns) noexcept {
    hists_[static_cast<std::size_t>(s)].record(ns);
  }

  void record_ticks(Stage s, std::uint64_t ticks) noexcept {
    hists_[static_cast<std::size_t>(s)].record(static_cast<std::int64_t>(tsc::to_nanos(ticks)));
  }

  [[nodiscard]] Histogram&       operator[](Stage s) noexcept       { return hists_[static_cast<std::size_t>(s)]; }
  [[nodiscard]] const Histogram& operator[](Stage s) const noexcept { return hists_[static_cast<std::size_t>(s)]; }

  void merge(const LatencyRecorder& other) noexcept {
    for (std::size_t i = 0; i < kStageCount; ++i) hists_[i].merge(other.hists_[i]);
  }

  void reset() noexcept { for (auto& h : hists_) h.reset(); }

  // Aligned table, one row per stage that saw traffic.
  [[nodiscard]] std::string report() const;

 private:
  std::vector<Histogram> hists_;
};

}  // namespace lob
