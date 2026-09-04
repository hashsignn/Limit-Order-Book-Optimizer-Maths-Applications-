// Latency model.
//
// Without this a simulator optimises a strategy that cannot exist: one that
// sees the book and acts on it in the same instant. Real orders take time to
// arrive, and the book moves while they are in flight. Two consequences, both
// of which cost money and neither of which appears in a zero-latency backtest:
//
//   - a cancel can be TOO LATE. You decide to pull a quote, and it trades
//     before your cancel lands. That is adverse selection with a clock on it.
//   - a new quote arrives at a book that has already moved, so it joins a queue
//     you did not intend or crosses a spread you did not mean to cross.
//
// arXiv:2505.12465 notes most published RL market-making ignores latency
// entirely, which is why those policies are unimplementable. Moallemi & Sağlam
// (Operations Research 61(5)) put a dollar figure on the delay.
//
// The distribution matters more than the mean. Latency is multi-modal — warm
// path, cold path, and the occasional interrupt — so this samples from an
// empirical distribution when given one, and falls back to a lognormal, which
// is a far better default than a constant.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <queue>
#include <random>
#include <vector>

#include "lob/core/types.hpp"

namespace lob {

struct LatencyConfig {
  // Defaults are the crypto-over-WebSocket row from docs/00: this project is a
  // model, and that is the venue class it can actually reach.
  Nanos  median_ns      = 1'000'000;    // 1 ms
  double sigma          = 0.6;          // lognormal shape; ~2.5x spread p50->p99
  Nanos  floor_ns       = 50'000;       // no message beats the wire
  // Feed and order entry are not symmetric: inbound is a multicast/broadcast
  // hop, outbound goes through a gateway and a risk check.
  double outbound_scale = 1.3;
  std::uint64_t seed    = 20260904;
};

class LatencyModel {
 public:
  explicit LatencyModel(LatencyConfig cfg = {})
      : cfg_(cfg), rng_(cfg.seed),
        dist_(std::log(static_cast<double>(cfg.median_ns)), cfg.sigma) {}

  // Market data reaching us: exchange publish -> our feed handler.
  [[nodiscard]] Nanos inbound() noexcept { return draw(1.0); }
  // Our decision reaching the exchange: send -> matching engine.
  [[nodiscard]] Nanos outbound() noexcept { return draw(cfg_.outbound_scale); }

  // Replace the parametric model with a measured one. Phase 2b will produce
  // these from real data; until then the lognormal is an assumption, and
  // labelling it as such is the point.
  void set_empirical(std::vector<Nanos> samples) {
    empirical_ = std::move(samples);
    std::sort(empirical_.begin(), empirical_.end());
  }
  [[nodiscard]] bool is_empirical() const noexcept { return !empirical_.empty(); }

 private:
  [[nodiscard]] Nanos draw(double scale) noexcept {
    double v;
    if (!empirical_.empty()) {
      v = static_cast<double>(empirical_[rng_() % empirical_.size()]);
    } else {
      v = dist_(rng_);
    }
    v *= scale;
    return std::max(cfg_.floor_ns, static_cast<Nanos>(v));
  }

  LatencyConfig                cfg_;
  std::mt19937_64              rng_;
  std::lognormal_distribution<double> dist_;
  std::vector<Nanos>           empirical_;
};

// What our strategy asked the exchange to do. Queued until its arrival time,
// because between deciding and arriving the world carries on without us.
enum class ActionType : std::uint8_t { Limit, Market, Cancel };

struct Action {
  Nanos      decided_ts = 0;   // when we decided
  Nanos      arrive_ts  = 0;   // when the exchange will see it
  ActionType type       = ActionType::Limit;
  OrderId    id         = 0;
  Side       side       = Side::Bid;
  Ticks      price      = 0;
  Qty        qty        = 0;
};

// Actions in flight, ordered by arrival. Ties break on decision time so the
// simulation stays deterministic — two messages arriving in the same nanosecond
// must resolve the same way on every run, or the journal will not replay.
class InFlight {
 public:
  void push(const Action& a) { q_.push(a); }

  [[nodiscard]] bool has_arrived_by(Nanos t) const noexcept {
    return !q_.empty() && q_.top().arrive_ts <= t;
  }
  [[nodiscard]] const Action& next() const noexcept { return q_.top(); }
  void pop() { q_.pop(); }

  [[nodiscard]] std::size_t size() const noexcept { return q_.size(); }
  [[nodiscard]] bool empty() const noexcept { return q_.empty(); }

 private:
  struct Later {
    bool operator()(const Action& a, const Action& b) const noexcept {
      if (a.arrive_ts != b.arrive_ts) return a.arrive_ts > b.arrive_ts;
      if (a.decided_ts != b.decided_ts) return a.decided_ts > b.decided_ts;
      return a.id > b.id;
    }
  };
  std::priority_queue<Action, std::vector<Action>, Later> q_;
};

}  // namespace lob
