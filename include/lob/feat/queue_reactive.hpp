// Queue-reactive intensity measurement.
//
// Huang, Lehalle & Rosenbaum (arXiv:1312.0563) model a book as a set of queues
// whose event intensities depend on the queue's own SIZE: a thin queue attracts
// adds and is cancelled out of quickly, a thick one is slow to move. That
// state dependence is the whole model, and it is what a Poisson generator with
// fixed rates cannot produce at any parameterisation.
//
// This class measures it. For each (side, level from the touch, queue-size
// bucket) it counts adds, cancels and trades, and accumulates the TIME the
// queue spent in that state. The intensity is then events over exposure, the
// same estimator the fill hazard uses and for the same reason: a queue that sat
// at a size and had nothing happen is evidence about that size, and dropping it
// would bias every rate upward.
//
// WHAT IS DELIBERATELY NOT DONE HERE
//
// No fitting, no smoothing, no parametric form. This writes counts and seconds;
// tools/fit_queue_reactive.py decides what lambda(q) looks like. Measurement and
// modelling in one file is how a modelling assumption ends up looking like an
// observation.
//
// Buckets are powers of two of the raw quantity, which needs no reference scale
// and so cannot be wrong about one. That is a finer grid than any model wants —
// the point is that regrouping coarse buckets is impossible and regrouping fine
// ones is a sum.
//
// AND OF THE ORDER COUNT, which is a separate axis for a reason.
//
// The mechanism this measurement exists to find is independent per-order
// cancellation: if every resting order cancels on its own clock, a queue with
// twice as many ORDERS in it produces twice the cancels, and lambda_cancel is
// proportional to the count. Share quantity is not that count. A queue of six
// million shares may be three large orders or three hundred small ones, and
// keyed on quantity alone the two are the same bucket.
//
// Measured on quantity alone, ethusd returned a cancel slope of -0.03 +- 0.08,
// which rejects proportionality at better than twelve standard errors and would
// have been reported as "real books do not cancel independently". Holding the
// count fixed and varying quantity, and the reverse, is what tells those apart.
#pragma once

#include <cstdint>
#include <cstdio>

#include "lob/book/events.hpp"
#include "lob/book/order_book.hpp"
#include "lob/core/types.hpp"

namespace lob {

class QueueReactive {
 public:
  static constexpr int kLevels  = 5;    // levels from the touch, each side
  static constexpr int kBuckets = 32;   // log2 of queue size, so up to 4e9
  static constexpr int kOrders  = 8;    // log2 of the order count, so up to 255
  enum Ev { kAdd = 0, kCancel = 1, kTrade = 2, kNumEv = 3 };

  // Call BEFORE the event is applied: the intensity at a queue size is the rate
  // of events that arrive while the queue IS that size, so the state that
  // matters is the one the event found, not the one it left behind.
  void on_event(const OrderBook& b, const BookEvent& e) {
    if (!b.has_bid() || !b.has_ask()) return;
    int ev;
    switch (e.type) {
      case EventType::Add:     ev = kAdd;    break;
      case EventType::Execute: ev = kTrade;  break;
      case EventType::Delete:
      case EventType::Reduce:  ev = kCancel; break;
      // A Replace is a cancel and an add at once, and counting it as either
      // would misattribute the other half. Both halves are counted.
      case EventType::Replace: ev = -1;      break;
      default: return;
    }
    const int s   = (e.side == Side::Bid) ? 0 : 1;
    const Ticks t = (e.side == Side::Bid) ? b.best_bid() : b.best_ask();
    const Ticks d = (e.side == Side::Bid) ? (t - e.price) : (e.price - t);
    if (d < 0 || d >= kLevels) return;
    const int lvl = static_cast<int>(d);
    const int q   = bucket(b.qty_at(e.side, e.price), kBuckets);
    const int n   = bucket(b.orders_at(e.side, e.price), kOrders);
    if (ev < 0) { ++counts_[s][lvl][q][n][kCancel]; ++counts_[s][lvl][q][n][kAdd]; }
    else        { ++counts_[s][lvl][q][n][ev]; }
  }

  // Call AFTER the event is applied, to accrue the time each queue spent at its
  // size. Every level is re-read because one event moves the touch and so
  // relabels every level behind it.
  void on_state(const OrderBook& b, Nanos now) {
    if (!b.has_bid() || !b.has_ask()) { last_ = now; return; }
    const double dt = last_ == 0 ? 0.0 : static_cast<double>(now - last_) / 1e9;
    for (int s = 0; s < 2; ++s) {
      const Side side = (s == 0) ? Side::Bid : Side::Ask;
      const Ticks t   = (s == 0) ? b.best_bid() : b.best_ask();
      for (int l = 0; l < kLevels; ++l) {
        const Ticks px = (s == 0) ? (t - l) : (t + l);
        const int q = bucket(b.qty_at(side, px), kBuckets);
        const int n = bucket(b.orders_at(side, px), kOrders);
        if (dt > 0.0 && cur_q_[s][l] >= 0) expo_[s][l][cur_q_[s][l]][cur_n_[s][l]] += dt;
        cur_q_[s][l] = q;
        cur_n_[s][l] = n;
      }
    }
    last_ = now;
  }

  void write(std::FILE* f) const {
    std::fprintf(f, "side,level,log2_qty,log2_orders,adds,cancels,trades,exposure_s\n");
    for (int s = 0; s < 2; ++s)
      for (int l = 0; l < kLevels; ++l)
        for (int q = 0; q < kBuckets; ++q)
          for (int n = 0; n < kOrders; ++n) {
            const std::uint64_t a = counts_[s][l][q][n][kAdd],
                                c = counts_[s][l][q][n][kCancel],
                                t = counts_[s][l][q][n][kTrade];
            if (a == 0 && c == 0 && t == 0 && expo_[s][l][q][n] <= 0.0) continue;
            std::fprintf(f, "%d,%d,%d,%d,%llu,%llu,%llu,%.6f\n", s, l, q, n,
                         static_cast<unsigned long long>(a),
                         static_cast<unsigned long long>(c),
                         static_cast<unsigned long long>(t), expo_[s][l][q][n]);
          }
  }

 private:
  // floor(log2(q)), so bucket b covers [2^b, 2^(b+1)). An empty queue is its
  // own bucket 0, because a queue nobody is in is a different state from a
  // queue with one lot in it -- the reference price moves off an empty one.
  // One overload, because Qty and the order count are different integer types
  // and letting the compiler choose between two was ambiguous for neither.
  // Negative is not a queue: it clamps to the empty bucket rather than
  // shifting a negative, which is undefined.
  template <typename T>
  [[nodiscard]] static int bucket(T v, int cap) noexcept {
    if (v <= T{0}) return 0;
    auto u = static_cast<std::uint64_t>(v);
    int b = 0;
    while (u > 1 && b < cap - 1) { u >>= 1; ++b; }
    return b;
  }

  std::uint64_t counts_[2][kLevels][kBuckets][kOrders][kNumEv] = {};
  double        expo_[2][kLevels][kBuckets][kOrders] = {};
  int           cur_q_[2][kLevels] = {{-1, -1, -1, -1, -1}, {-1, -1, -1, -1, -1}};
  int           cur_n_[2][kLevels] = {{-1, -1, -1, -1, -1}, {-1, -1, -1, -1, -1}};
  Nanos         last_ = 0;
};

}  // namespace lob
