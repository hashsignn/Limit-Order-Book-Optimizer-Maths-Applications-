// Synthetic order flow.
//
// Two jobs, and it is worth being clear which is which:
//
//  1. NOW (Phase 1): drive the book hard enough to prove it correct. For that,
//     realism is beside the point — what matters is coverage. The generator is
//     tuned to produce adds, partial cancels, full cancels, partial and full
//     fills, replaces, empty levels, level clears and touch moves in quick
//     succession, including the awkward orderings a real feed produces rarely.
//
//  2. LATER (Phase 3): the same interface, backed by a calibrated queue-reactive
//     or Hawkes model, becomes the simulator. This is a zero-intelligence
//     Poisson generator — the baseline the roadmap says a real simulator must
//     beat, and the one Smith, Farmer, Gillemot & Krishnamurthy (2003) analyse.
//
// It does NOT reproduce the stylized facts in docs/03-metrics-and-estimators.md
// §11, and nothing calibrated should be fitted to it.
#pragma once

#include <algorithm>
#include <cstdint>
#include <random>
#include <unordered_map>
#include <vector>

#include "lob/book/events.hpp"
#include "lob/core/types.hpp"

namespace lob {

struct FlowConfig {
  Ticks         mid            = 10'000;   // starting mid, in ticks
  Ticks         half_spread    = 1;
  std::uint32_t levels         = 10;       // how far from the touch orders land

  // WHERE an add lands, as a share of adds per level from the touch.
  //
  // This was uniform, and uniform is not what a book does. Measured on the
  // ethusd capture in data/samples, 47.8% of adds join the BEST queue and the
  // rest decay away from it, while the generator put 32.6% at the touch and
  // MORE one tick behind it than at it:
  //
  //                        L0     L1     L2     L3     L4     L5
  //     ethusd adds     47.8%  13.2%   9.6%   9.1%   9.9%  10.4%
  //     generator       32.6%  20.9%  14.0%  11.8%  10.5%  10.1%
  //
  // and the standing profile follows: ethusd rests 69.9% of its near-touch
  // orders AT the touch, the generator 28.8%.
  //
  // What this is NOT. It is not the reason the touch was too stable -- that
  // was the level of the book, three orders of magnitude of it, and this shape
  // barely moves the move rate. It is also not evidence that touch orders are
  // short-lived: dividing the two profiles above gives a relative lifetime of
  // 1.46 at the touch against 0.45 to 0.71 behind it, so an order at the touch
  // lives LONGER than one behind it, which is the opposite of what was assumed
  // before it was measured. See docs/KNOWN-ISSUES.md 4.
  //
  // The weights are the placement shape and nothing more. They are set from the
  // measured ADD distribution rather than the standing one, because placement
  // is what this function chooses; the standing profile is an outcome.
  //
  // The numbers are the ethusd row above, from the ten-minute capture committed
  // in data/samples so that anyone can reproduce them. The eight-hour capture
  // agrees within a few points (52.6 / 14.2 / 11.2 / 11.2 / 10.8) and is not
  // in the repository.
  //
  // Levels beyond the sixth are an extrapolation, held at the level-5 rate,
  // because the measurement covers six. Say so rather than imply the shape was
  // measured all the way out.
  static constexpr std::size_t kMaxLevels = 16;
  double add_level_weight[kMaxLevels] = {47.8, 13.2,  9.6,  9.1,  9.9, 10.4, 10.4, 10.4,
                                         10.4, 10.4, 10.4, 10.4, 10.4, 10.4, 10.4, 10.4};
  Qty           min_qty        = 1;
  Qty           max_qty        = 500;
  // Relative weights, FITTED to the event mix measured within five levels of
  // the touch on the ethusd captures: adds 43.5%, cancels 53.9%, trades 2.60%.
  // These reproduce 45.8 / 51.7 / 2.45 -- close, and stated rather than
  // asserted. The weights are not the mix: an aggressive order produces several
  // trade events, an add can be throttled by target_live, and a replace is both
  // an add and a cancel, so the map from one to the other was searched, not
  // solved.
  //
  // The generator previously ran adds 50.9%, cancels 38.7%, and 14.2% of events
  // producing a trade one way or another -- more than five times the real trade
  // rate, on a book where cancels dominate.
  double        w_add          = 0.42;
  double        w_delete       = 0.38;
  double        w_reduce       = 0.13;
  double        w_replace      = 0.04;

  // FABRICATED FILLS. Zero for the simulator, on purpose.
  //
  // An Execute here is emitted on a uniformly random resting order chosen from
  // anywhere in the book: no aggressor caused it, it consumes nothing from the
  // front of any queue, and its victim is picked without reference to queue
  // position. Measured, that path carried 56.2% of the simulator's fills by
  // count and 87.0% by volume -- the skew because it takes the whole resting
  // order half the time while an aggressive order is usually one to forty lots.
  //
  // Queue position is the state variable the entire Phase 5 MDP exists to
  // exploit, and a fill drawn uniformly over resting orders is independent of
  // it. Eighty-seven per cent of the volume was averaging that signal away
  // inside the process the acceptance test is measured on.
  //
  // It stays available because the generator has a second job: driving the book
  // hard enough to prove it correct, where exercising the Execute path is the
  // point (tests/test_book_differential, test_properties, test_features and
  // bench/bench_book all set it). The default is zero because of which mistake
  // is worse. A book test that loses Execute coverage still passes and covers
  // less; a simulator with fabricated fills still runs and answers a different
  // question. The dangerous one should be the one you have to ask for.
  double        w_execute      = 0.0;
  // Aggressive orders that cross the spread. Without these nothing ever trades
  // against a resting quote, so the fill model is never exercised — which is
  // the one thing a market-making simulator has to get right.
  double        w_aggress      = 0.025;
  Qty           aggress_max    = 400;

  // ---- informed and uninformed flow (Glosten & Milgrom 1985) --------------
  //
  // A market maker is paid by traders who trade for reasons unrelated to value
  // and taxed by traders who know something. Both populations have to exist or
  // quoting makes no sense: with no informed flow there is no adverse selection
  // to price, and with nothing but informed flow there is no reason to quote at
  // all.
  //
  // This generator had NEITHER. Its mid stepped on an independent coin flip,
  // uncorrelated with any trade, so a fill carried no information and the price
  // moved for reasons a maker could neither anticipate nor be compensated for.
  // Every strategy lost money, the optimal action was to stop quoting, and
  // Phase 5's acceptance test was therefore unanswerable on it — the best
  // baseline was whichever one traded least.
  //
  // Now a share of aggressive orders are informed: the value moved, and this
  // order is the first sign of it, so the mid FOLLOWS the trade. The rest are
  // uninformed and the mid does not move behind them. A maker keeps the spread
  // from the second group and pays impact to the first, which is the whole
  // economics of the business and the thing that was missing.
  //
  // Not every informed trade moves the price. Information arrives in pieces and
  // the price moves when enough of it accumulates, so an informed order moves
  // the mid a tick only with probability informed_impact_prob.
  //
  // HOW BIG THIS CHANNEL HAS TO BE, and how that was decided.
  //
  // It used to be argued from a bound. The volatility a quote is exposed to
  // over its lifetime L has to stay under the half-spread it earns:
  //
  //     L * (drift_prob + p_aggress * pi * informed_impact_prob)  <  half_spread^2
  //
  // with L in EVENTS, put at 7,500 because a quote rested about 15 ms and an
  // event took 2 us. That gave pi * informed_impact_prob < 0.0021, and
  // informed_impact_prob = 0.01 followed from it.
  //
  // Both inputs to that arithmetic were wrong. The 2 us was an uncalibrated
  // clock (see mean_gap_ns) and the 15 ms came from a message budget counted in
  // events. The bound is also the wrong shape: it says when a market is
  // QUOTABLE, which is a ceiling, and a ceiling does not pick a value. Any
  // informed_impact_prob below it satisfies it, including zero, and zero is a
  // market with no adverse selection at all.
  //
  // So it is fitted now, to the one thing the captures state directly:
  // P(the mid has moved against the resting side one second after a print).
  // tools/mdp_params.py reports it as p_adverse_1.0s.
  //
  //     ethusd, 8-hour capture, n=5,032 prints      33%
  //     ethusd, 10-minute sample, n=54              48%
  //     xrpusd, 10-minute sample, n=153             52%
  //     btcusd, 10-minute sample, n=220             63%
  //
  //     generator, at pi = 0.20:
  //       informed_impact_prob   0.01   0.10   0.50   0.75   0.85   1.00
  //       p_adverse             13.6%  16.1%  26.2%  31.8%  33.4%  36.2%
  //
  // 0.85 reproduces the large-sample figure, and at pi = 0.50 the same value
  // gives 50.7%, inside the range the three small samples span. At the old
  // 0.01 the generator sits at 13.6% -- a market that barely punishes a maker
  // at all, which is what made `evaluate --sweep-informed` flat from pi = 0 to
  // pi = 0.7 and left the acceptance test with nothing to measure.
  //
  // TWO CAVEATS, both real.
  //
  // The measurement identifies the PRODUCT pi * informed_impact_prob, not the
  // split. pi stays at 0.20 because it is the sweep's variable and moving it to
  // a convenient place would be fitting the diagnostic to its own answer;
  // pi = 0.5 with impact 0.75 fits the small samples equally well.
  //
  // And 0.85 is close to its ceiling of 1.0, which undercuts the reason this
  // parameter exists -- information arriving in pieces, so that an informed
  // order moves the price only sometimes. Read plainly, it says this generator
  // cannot produce the observed adverse selection at a realistic informed share
  // without making informed orders nearly always move the price. The missing
  // mechanism is the one the captures show and this model does not have: most
  // of what moves a real touch is quotes being PULLED, not anything trading --
  // only 4.4% of ethusd's touch moves follow a print within 50 ms -- and orders
  // pulled ahead of a price move are queue-reactive cancellation, which is
  // Huang, Lehalle & Rosenbaum's mechanism and is not implemented here.
  //
  // sweep-informed measures the curve rather than trusting any of this, and it
  // is the thing to re-run if any of these change.
  double        informed_frac        = 0.20;   // pi
  double        informed_impact_prob = 0.85;   // chance an informed order moves the mid
  Ticks         informed_impact      = 1;      // ticks it moves when it does

  // Exogenous news: the mid moving with no trade behind it at all. Real prices
  // do that, so it is kept — but it has a ceiling, and the ceiling is
  // derivable rather than a matter of taste.
  //
  // A resting quote does not move with the mid. Whenever the mid walks away
  // from it the quote is picked off, and CONDITIONAL ON BEING FILLED the walk
  // is adverse — that is a cost with no offsetting revenue, unlike informed
  // flow, which at least pays the spread on the way through. Over a quote's
  // lifetime L events the walk is sqrt(L * drift_prob) ticks, and for a maker
  // to survive it that has to stay under the half-spread it earns:
  //
  //     L * drift_prob  <  half_spread^2
  //
  // Quotes here live about 15 ms, which at ~2 us between events is L = 7500,
  // and half_spread is 1 tick. So L * drift_prob must stay well under 1, and
  // it has to leave room for the informed flow below rather than spending the
  // whole budget itself.
  //
  // It was 0.02 — a 12-tick walk against a 1-tick edge, a hundred and fifty
  // times over. That single number was why every strategy lost money in the
  // Phase 5 evaluation and why the optimal action was to stop quoting.
  double        drift_prob     = 1e-5;
  // How many orders the book holds, held there by a TWO-SIDED controller.
  //
  // This used to be a ceiling: past it, adds were suppressed. That works only
  // while adds outnumber removals, and it silently does nothing when they do
  // not. Fitting the weights to the measured near-touch event mix -- where
  // cancels outnumber adds, because orders drift in from levels further out --
  // flipped the balance, and the book drained from 20,001 resting orders to
  // TWENTY with nothing to stop it. Every test and every simulation then ran
  // against an almost empty book, and all of them still passed.
  //
  // A real venue's book is stationary because entry and exit balance there too,
  // and reproducing that is the controller's job, not the weights'. Freeing the
  // weights of it is what lets the mix be fitted to data at all.
  std::size_t   target_live    = 20'000;

  // Mean nanoseconds between events. This was `1 + rng() % 5000` written inline
  // -- a 2.5 us mean, so ~400,000 events a second, chosen for no reason beyond
  // making a test run quickly.
  //
  // ethusd on Bitstamp runs at 53.9 events a second within 2% of the mid. The
  // default here is four orders of magnitude off, and that mattered: the
  // per-SECOND touch move rate is the product of the per-event rate and this
  // number, so an error here can cancel an error there and did. See
  // FlowConfig::ethusd() and docs/KNOWN-ISSUES.md issue 4.
  Nanos         mean_gap_ns    = 2'500;
  std::uint64_t seed           = 20260904;

  // ---- the calibration ----------------------------------------------------
  // Everything above is the STRESS configuration: a dense, fast book whose job
  // is to drive the matching engine through every ordering it can produce. It
  // is deliberately not a market, and the differential and property tests want
  // it exactly as it is.
  //
  // This is the other job. Measured on the three ten-minute captures in
  // data/samples, per event and pooling both sides:
  //
  //                        ethusd   btcusd   xrpusd  |  stress cfg
  //     events/second        53.9     96.0     57.4  |    400,000
  //     orders at touch       3.8      4.5      3.1  |        857
  //     touch moves/event  3.3e-2   6.5e-2   6.9e-2  |     8.0e-5
  //     touch moves/second    1.80     6.23     3.95  |      32.0
  //
  // Three instruments, one venue, and they agree: a touch is three to five
  // orders, a few dozen events arrive a second, and three to seven per cent of
  // them move the price. The stress configuration is four orders of magnitude
  // out on the first two and they hid each other -- the per-second rate is the
  // product of the per-event rate and the clock, so a book 200x too thick and a
  // clock 7,400x too fast came out looking merely 18x too volatile, and with
  // the sign reversed. It was recorded that way. See docs/KNOWN-ISSUES.md 4.
  //
  // What a touch that cannot be consumed costs: the price then moves only when
  // the exogenous walk moves it, so a fill carries no information, and adverse
  // selection -- the entire risk a market maker is paid to bear -- becomes
  // noise the policy can neither predict nor be compensated for.
  //
  // target_live is fitted, not derived. It is the only free parameter here and
  // the response to it is STEEP -- doubling it from 16 to 32 divides the move
  // rate by nineteen -- so this is a knife-edge and not a law:
  //
  //     target_live      8      12      16      24      32   |  ethusd
  //     at touch       3.6     4.9     6.3     9.3    12.2   |     3.8
  //     spread        2.90    2.33    2.12    2.01    1.99   |     2.3
  //     moves/event  1.0e-1  4.3e-2  1.9e-2  3.6e-3  9.4e-4  |  3.3e-2
  //     one-sided     1.8%    0.2%    0.0%    0.0%    0.0%   |     --
  //
  // Twelve, because it is the only value within 30% on all four at once. Eight
  // matches the touch count best and then overshoots the move rate threefold
  // and leaves a side of the book empty 1.8% of the time.
  [[nodiscard]] static FlowConfig ethusd() noexcept {
    FlowConfig c;
    c.levels      = 8;
    c.target_live = 12;
    // 1 s / 53.9 events. next() draws uniformly on [1, 2*mean), so the mean of
    // the draw is mean_gap_ns.
    c.mean_gap_ns = 18'550'000;
    return c;
  }
};

// Emits a stream that is always *structurally* valid: it only cancels or fills
// orders it knows are resting, and never quotes a bid through an ask. A feed
// with genuine gaps is a separate test case, driven by corrupting this output.
class FlowGenerator {
 public:
  // Bounds on the add-rate controller. Wide enough to refill an empty book or
  // drain an overfull one quickly, narrow enough that the event mix near the
  // target is the fitted one rather than the controller's.
  static constexpr double kMinAddGain = 0.15;
  static constexpr double kMaxAddGain = 8.0;

  explicit FlowGenerator(FlowConfig cfg = {})
      : cfg_(cfg), rng_(cfg.seed), mid_(cfg.mid) {}

  [[nodiscard]] BookEvent next() noexcept {
    BookEvent e{};
    e.ts  = ts_;
    e.seq = seq_++;
    // Uniform on [1, 2*mean_gap_ns), so the mean gap is mean_gap_ns. Poisson
    // arrivals would be exponential rather than uniform; that is a real
    // difference and it is not modelled here, because nothing downstream reads
    // the gap DISTRIBUTION -- only the rate, which this gets right.
    ts_  += 1 + static_cast<Nanos>(rng_() % static_cast<std::uint64_t>(
                    cfg_.mean_gap_ns > 0 ? 2 * cfg_.mean_gap_ns : 1));

    // Impact from the last informed trade lands before this event, so the
    // aggressor that carried the information traded at the OLD price and
    // whoever supplied it is now holding at the new one. That ordering is the
    // adverse selection; reversing it would pay the maker for being run over.
    if (pending_impact_ != 0) {
      mid_ += pending_impact_;
      pending_impact_ = 0;
    }
    if (uniform() < cfg_.drift_prob) drift();

    // Nothing resting yet: only Add is legal.
    const bool can_touch_existing = !live_.empty();
    // Scale the add weight by how far the book is from its target: a book at
    // half size adds twice as eagerly, one at double size a quarter as much.
    // Clamped, so neither end can run away, and equal to cfg_.w_add exactly at
    // target -- so the fitted event mix is what the book actually produces when
    // it is where it should be.
    const double fill = cfg_.target_live > 0
        ? static_cast<double>(live_.size()) / static_cast<double>(cfg_.target_live)
        : 1.0;
    const double gain  = fill <= 0.0 ? kMaxAddGain
                       : std::min(kMaxAddGain, std::max(kMinAddGain, 1.0 / (fill * fill)));
    const double w_add = cfg_.w_add * gain;
    const double total = w_add + cfg_.w_delete + cfg_.w_reduce + cfg_.w_execute
                       + cfg_.w_replace + cfg_.w_aggress;
    const double r = uniform() * total;

    double acc = w_add;
    if (r < acc || !can_touch_existing) return make_add(e);
    acc += cfg_.w_delete;
    if (r < acc) return make_on_existing(e, EventType::Delete);
    acc += cfg_.w_reduce;
    if (r < acc) return make_on_existing(e, EventType::Reduce);
    acc += cfg_.w_execute;
    if (r < acc) return make_on_existing(e, EventType::Execute);
    acc += cfg_.w_replace;
    if (r < acc) return make_on_existing(e, EventType::Replace);
    return make_aggress(e);
  }

  // The generator tracks what it believes is resting so it can emit valid
  // references. Call after applying, to keep belief and book in step.
  void on_applied(const BookEvent& e, Qty resting_after) noexcept {
    switch (e.type) {
      case EventType::Add:
        remember(Live{e.order_id, e.qty, e.price, e.side});
        break;
      case EventType::Delete:
        forget(e.order_id);
        break;
      case EventType::Reduce:
      case EventType::Execute: {
        if (resting_after <= 0) { forget(e.order_id); break; }
        auto it = at_.find(e.order_id);
        if (it != at_.end()) live_[it->second].qty = resting_after;
        break;
      }
      case EventType::Replace:
        forget(e.order_id);
        remember(Live{e.new_id, e.qty, e.price, e.side});
        break;
      case EventType::Clear:
        live_.clear();
        at_.clear();
        break;
      case EventType::Aggress:
        // The matcher decides what this consumed; the caller resyncs via
        // forget_order() for each order it removed.
        break;
      case EventType::Count: break;
    }
  }

  // The generator is told the current touch after each event. A real venue
  // never *books* a crossing order — it matches it — so an MBO feed contains no
  // crossing adds. Without this the drifting mid eventually quotes bids through
  // resting asks and produces a stream no exchange would ever emit.
  void observe(bool has_bid, Ticks bid, bool has_ask, Ticks ask) noexcept {
    has_bid_ = has_bid; best_bid_ = bid;
    has_ask_ = has_ask; best_ask_ = ask;
  }

  // Told by the caller when a match removed a resting order the generator still
  // believed was live. Without this the generator emits references to orders
  // that an aggressive order already consumed.
  void forget_order(OrderId id) { forget(id); }

  [[nodiscard]] std::size_t believed_live() const noexcept { return live_.size(); }
  [[nodiscard]] Ticks mid() const noexcept { return mid_; }
  // How the aggressive flow split, so a run can report the population it was
  // actually drawn against rather than the one that was configured.
  [[nodiscard]] std::uint64_t informed_trades() const noexcept { return informed_; }
  [[nodiscard]] std::uint64_t uninformed_trades() const noexcept { return uninformed_; }

 private:
  struct Live { OrderId id; Qty qty; Ticks price; Side side; };

  // Weighted choice of how far from the touch an add lands. Linear scan over at
  // most sixteen weights, which costs nothing next to the book update that
  // follows and keeps the distribution in one readable place.
  [[nodiscard]] std::uint32_t pick_level() noexcept {
    const std::uint32_t n = cfg_.levels < FlowConfig::kMaxLevels
                          ? cfg_.levels : static_cast<std::uint32_t>(FlowConfig::kMaxLevels);
    if (n == 0) return 0;
    double total = 0.0;
    for (std::uint32_t i = 0; i < n; ++i) total += cfg_.add_level_weight[i];
    if (!(total > 0.0)) return static_cast<std::uint32_t>(rng_() % n);
    double r = uniform() * total;
    for (std::uint32_t i = 0; i < n; ++i) {
      r -= cfg_.add_level_weight[i];
      if (r <= 0.0) return i;
    }
    return n - 1;
  }

  [[nodiscard]] double uniform() noexcept {
    return static_cast<double>(rng_() >> 11) * (1.0 / 9007199254740992.0);
  }
  [[nodiscard]] Qty rand_qty() noexcept {
    return cfg_.min_qty + static_cast<Qty>(rng_() % static_cast<std::uint64_t>(cfg_.max_qty - cfg_.min_qty + 1));
  }

  void drift() noexcept { mid_ += (rng_() & 1) ? 1 : -1; }

  // Prices are drawn from the touch outward, which keeps the book shaped
  // roughly like a real one and, more importantly for testing, keeps levels
  // shallow enough that they empty and re-fill constantly.
  [[nodiscard]] Ticks price_for(Side s) noexcept {
    const auto away = static_cast<Ticks>(pick_level());
    Ticks p = (s == Side::Bid) ? mid_ - cfg_.half_spread - away
                               : mid_ + cfg_.half_spread + away;
    // Clamp to the near side of the touch, so the stream stays non-crossing
    // even as the mid drifts away from where the resting orders sit.
    if (s == Side::Bid && has_ask_ && p >= best_ask_) p = best_ask_ - 1;
    if (s == Side::Ask && has_bid_ && p <= best_bid_) p = best_bid_ + 1;
    return p;
  }

  BookEvent make_add(BookEvent e) noexcept {
    e.type     = EventType::Add;
    e.side     = (rng_() & 1) ? Side::Bid : Side::Ask;
    e.price    = price_for(e.side);
    e.qty      = rand_qty();
    e.order_id = next_id_++;
    return e;
  }

  // A market order sweeping the opposite side. Size is drawn small most of the
  // time and occasionally large, so both "nibbles the front of the queue" and
  // "clears the level" are exercised.
  BookEvent make_aggress(BookEvent e) noexcept {
    e.type     = EventType::Aggress;
    e.side     = (rng_() & 1) ? Side::Bid : Side::Ask;
    e.order_id = next_id_++;
    e.price    = 0;
    const bool big = (rng_() % 10) == 0;
    e.qty = big ? 1 + static_cast<Qty>(rng_() % static_cast<std::uint64_t>(cfg_.aggress_max))
                : 1 + static_cast<Qty>(rng_() % 40);

    // Informed: the value moved and this order is acting on it, so the mid
    // follows. A Bid aggressor is buying, which lifts the ask and takes the
    // price up.
    if (uniform() < cfg_.informed_frac) {
      ++informed_;
      if (uniform() < cfg_.informed_impact_prob)
        pending_impact_ = (e.side == Side::Bid) ? cfg_.informed_impact : -cfg_.informed_impact;
    } else {
      ++uninformed_;
    }
    return e;
  }

  BookEvent make_on_existing(BookEvent e, EventType t) noexcept {
    const auto& l = live_[rng_() % live_.size()];
    e.type     = t;
    e.order_id = l.id;
    e.side     = l.side;
    e.price    = l.price;
    switch (t) {
      case EventType::Delete:
        e.qty = 0;
        break;
      case EventType::Reduce:
      case EventType::Execute:
        // Half the time take the whole thing, so full-fill and reduce-to-zero
        // paths get exercised as often as the partial ones.
        e.qty = (rng_() & 1) ? l.qty : 1 + static_cast<Qty>(rng_() % static_cast<std::uint64_t>(l.qty));
        break;
      case EventType::Replace:
        e.new_id = next_id_++;
        e.price  = price_for(l.side);
        e.qty    = rand_qty();
        break;
      default: break;
    }
    return e;
  }

  void remember(const Live& l) {
    at_[l.id] = live_.size();
    live_.push_back(l);
  }

  // Swap-and-pop, with the index of the moved element repaired. O(1) — a linear
  // scan here costs more than the book operation it is bookkeeping for.
  void forget(OrderId id) {
    auto it = at_.find(id);
    if (it == at_.end()) return;
    const std::size_t i = it->second;
    live_[i] = live_.back();
    at_[live_[i].id] = i;
    live_.pop_back();
    at_.erase(id);
  }

  FlowConfig        cfg_;
  std::mt19937_64   rng_;
  std::vector<Live>                       live_;
  std::unordered_map<OrderId, std::size_t> at_;   // id -> index into live_
  Ticks             mid_     = 0;
  Ticks             pending_impact_ = 0;
  std::uint64_t     informed_ = 0, uninformed_ = 0;
  bool              has_bid_ = false;
  bool              has_ask_ = false;
  Ticks             best_bid_ = 0;
  Ticks             best_ask_ = 0;
  OrderId           next_id_ = 1;
  SeqNum            seq_     = 1;
  Nanos             ts_      = 0;
};

}  // namespace lob
