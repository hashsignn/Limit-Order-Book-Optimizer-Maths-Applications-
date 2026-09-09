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
// THE QUEUE AXIS IS THE PAPER'S: q / AES, IN LINEAR STEPS
//
// This binned powers of two of the raw quantity, on the argument that a log
// grid needs no reference scale and so cannot be wrong about one. True, and
// beside the point. Huang et al. measure lambda against the queue size in units
// of the AVERAGE EVENT SIZE at that queue -- ceil(volume / AES_i) -- and every
// shape they report lives between 0 and 40 of those units: limit insertion flat
// at the touch and falling behind it, cancellation concave to about 25 and then
// flat, market orders decaying exponentially. A log2 grid puts that whole range
// into six buckets, so the thing being measured is gone before anything can be
// fitted to it. A grid that cannot be wrong about a scale and cannot see the
// answer either is not the safer choice.
//
// AES is measured here, not supplied. The warmup window the caller already
// discards pays for it: during warmup this only accumulates event sizes, and
// the scale freezes at the first measured event. Estimating the scale on a
// prefix and the intensities on the rest keeps the two out of each other's way.
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

#include <cmath>
#include <cstdint>
#include <cstdio>

#include "lob/book/events.hpp"
#include "lob/book/order_book.hpp"
#include "lob/core/types.hpp"

namespace lob {

class QueueReactive {
 public:
  static constexpr int kLevels  = 5;    // levels from the touch, each side
  // Queue size in AES units, linear. The paper's figures stop at 40; the last
  // bucket is everything beyond, so a queue far out of range is still counted
  // somewhere rather than dropped.
  static constexpr int kBuckets = 48;
  static constexpr int kOrders  = 8;    // log2 of the order count, so up to 255
  enum Ev { kAdd = 0, kCancel = 1, kTrade = 2, kNumEv = 3 };

  // The event size this queue is measured in, once frozen. Zero means the
  // warmup saw nothing here and the level was never binned.
  [[nodiscard]] double aes(int level) const noexcept {
    return (level >= 0 && level < kLevels) ? aes_[level] : 0.0;
  }
  [[nodiscard]] bool scaled() const noexcept { return frozen_; }

  // Call BEFORE the event is applied: the intensity at a queue size is the rate
  // of events that arrive while the queue IS that size, so the state that
  // matters is the one the event found, not the one it left behind.
  // `warmed` is the caller's own warmup flag. Before it turns true this only
  // learns the scale; after, it freezes the scale once and counts events.
  void on_event(const OrderBook& b, const BookEvent& e, bool warmed = true) {
    if (!b.has_bid() || !b.has_ask()) return;
    if (!warmed) { learn(b, e); return; }
    freeze();
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
    const int q   = qbucket(b.qty_at(e.side, e.price), lvl);
    const int n   = bucket(b.orders_at(e.side, e.price), kOrders);
    if (ev < 0) { ++counts_[s][lvl][q][n][kCancel]; ++counts_[s][lvl][q][n][kAdd]; }
    else        { ++counts_[s][lvl][q][n][ev]; }
  }

  // Call AFTER the event is applied, to accrue the time each queue spent at its
  // size. Every level is re-read because one event moves the touch and so
  // relabels every level behind it.
  void on_state(const OrderBook& b, Nanos now) {
    if (!b.has_bid() || !b.has_ask()) { last_ = now; return; }
    freeze();
    const double dt = last_ == 0 ? 0.0 : static_cast<double>(now - last_) / 1e9;
    for (int s = 0; s < 2; ++s) {
      const Side side = (s == 0) ? Side::Bid : Side::Ask;
      const Ticks t   = (s == 0) ? b.best_bid() : b.best_ask();
      for (int l = 0; l < kLevels; ++l) {
        const Ticks px = (s == 0) ? (t - l) : (t + l);
        const int q = qbucket(b.qty_at(side, px), l);
        const int n = bucket(b.orders_at(side, px), kOrders);
        if (dt > 0.0 && cur_q_[s][l] >= 0) expo_[s][l][cur_q_[s][l]][cur_n_[s][l]] += dt;
        cur_q_[s][l] = q;
        cur_n_[s][l] = n;
      }
    }
    last_ = now;
  }

  void write(std::FILE* f) const {
    // aes travels with the rows because q_aes is meaningless without it: two
    // captures of the same instrument have different average event sizes and
    // comparing their bucket 7 is comparing different queue sizes.
    std::fprintf(f, "side,level,q_aes,log2_orders,adds,cancels,trades,exposure_s,aes\n");
    for (int s = 0; s < 2; ++s)
      for (int l = 0; l < kLevels; ++l)
        for (int q = 0; q < kBuckets; ++q)
          for (int n = 0; n < kOrders; ++n) {
            const std::uint64_t a = counts_[s][l][q][n][kAdd],
                                c = counts_[s][l][q][n][kCancel],
                                t = counts_[s][l][q][n][kTrade];
            if (a == 0 && c == 0 && t == 0 && expo_[s][l][q][n] <= 0.0) continue;
            std::fprintf(f, "%d,%d,%d,%d,%llu,%llu,%llu,%.6f,%.4f\n", s, l, q, n,
                         static_cast<unsigned long long>(a),
                         static_cast<unsigned long long>(c),
                         static_cast<unsigned long long>(t), expo_[s][l][q][n],
                         aes_[l]);
          }
  }

 private:
  // ---- the scale ----------------------------------------------------------
  // The average size of an event at this queue, which is what the queue axis
  // counts in. A Delete carries no size of its own -- it removes whatever was
  // resting -- so its size is read off the book, which is why on_event must be
  // called BEFORE the event is applied.
  void learn(const OrderBook& b, const BookEvent& e) {
    if (frozen_) return;
    const int s   = (e.side == Side::Bid) ? 0 : 1;
    const Ticks t = (e.side == Side::Bid) ? b.best_bid() : b.best_ask();
    const Ticks d = (e.side == Side::Bid) ? (t - e.price) : (e.price - t);
    if (d < 0 || d >= kLevels) return;
    Qty size = 0;
    switch (e.type) {
      case EventType::Add:
      case EventType::Replace:
      case EventType::Reduce:
      case EventType::Execute: size = e.qty; break;
      case EventType::Delete:  size = b.qty_of(e.order_id); break;
      default: return;
    }
    if (size <= 0) return;
    // Pooled over sides, as the paper pools the intensities themselves:
    // lambda_i(n) = lambda_{-i}(n) by the book's symmetry. Per side, a
    // sixty-second warmup put level 4's average event at 100,536 on the bid
    // and 745,181 on the ask -- seven times apart on a quantity that is the
    // same by construction. Pooling halves the noise and asserts nothing the
    // model does not already assume.
    (void)s;
    warm_sum_[static_cast<int>(d)] += static_cast<double>(size);
    ++warm_n_[static_cast<int>(d)];
  }

  // Freeze once, at the first measured event. A level the warmup never saw
  // falls back to the pooled mean over the levels it did, and if it saw
  // nothing at all the scale is 1 and the axis is raw quantity -- degraded,
  // but reported through aes() rather than silently pretending otherwise.
  void freeze() noexcept {
    if (frozen_) return;
    frozen_ = true;
    double pooled = 0.0; std::uint64_t pooled_n = 0;
    for (int l = 0; l < kLevels; ++l) { pooled += warm_sum_[l]; pooled_n += warm_n_[l]; }
    const double fallback = pooled_n > 0 ? pooled / static_cast<double>(pooled_n) : 1.0;
    // A level needs enough events for its own mean to beat the pooled one.
    // Thirty is not a deep result, it is the point past which one outlier stops
    // moving the answer by a factor.
    constexpr std::uint64_t kMinForOwn = 30;
    for (int l = 0; l < kLevels; ++l)
      aes_[l] = warm_n_[l] >= kMinForOwn
              ? warm_sum_[l] / static_cast<double>(warm_n_[l])
              : fallback;
  }

  // ceil(volume / AES), clamped. Bucket 0 is an empty queue and nothing else:
  // a queue nobody is in is a different state from one holding a single event,
  // because the reference price moves off an empty queue and not off a thin
  // one. That is why this is a ceiling and not a rounding.
  [[nodiscard]] int qbucket(Qty v, int l) const noexcept {
    if (v <= Qty{0}) return 0;
    const double a = aes_[l] > 0.0 ? aes_[l] : 1.0;
    int q = static_cast<int>(std::ceil(static_cast<double>(v) / a));
    if (q < 1) q = 1;                       // 0 is reserved for an empty queue
    if (q >= kBuckets) q = kBuckets - 1;
    return q;
  }

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
  double        warm_sum_[kLevels] = {};
  std::uint64_t warm_n_[kLevels]   = {};
  double        aes_[kLevels]      = {};
  bool          frozen_            = false;
};

}  // namespace lob
