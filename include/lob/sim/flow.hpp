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
  // ---- Model I: rates that depend on the queue ----------------------------
  // Huang, Lehalle & Rosenbaum (arXiv:1312.0563), the model docs/06 specifies.
  // Everything above emits events at rates that do not depend on the book at
  // all, and a removal picks a uniformly random resting order, which is why the
  // generator's cancel intensity comes out proportional to the order count for
  // a reason that has nothing to do with the book being reactive.
  //
  // Here each (side, level) is a queue with its own birth-and-death rates, and
  // those rates are functions of that queue's own size measured in average
  // event sizes -- q = ceil(depth / aes), the paper's axis and now the
  // estimator's. The book is then 2K independent queues and the event stream
  // is a Markov jump process: total rate Lambda, one event drawn in proportion
  // to its own rate, time advanced by an exponential draw. The interevent
  // distribution comes out of the model rather than being imposed, which is
  // the second thing the fixed-weight path gets wrong.
  //
  // THE SHAPES, and where each comes from.
  //
  //   adds     Flat in q at the touch, decaying with q behind it, and LOWER at
  //            an empty queue. Measured +0.10 +- 0.24 on btcusd and +0.10 +-
  //            0.01 in the fixed-weight generator, so this is the one shape
  //            already right and the paper agrees. The drop at zero is the
  //            paper's: an order alone in an empty queue has created a new best
  //            limit and has no idea where the efficient price is.
  //
  //   cancels  Rising and concave, saturating -- the paper's Q1 curve rises to
  //            about 25 AES and then flattens, explicitly NOT the linear rate
  //            of Cont, Stoikov & Talreja, because priority is worth more in a
  //            long queue and people do not throw it away.
  //
  //   trades   Falling, close to exponential. This is the paper's central
  //            shape and the one the generator has BACKWARDS: takers rush for
  //            liquidity when it is scarce and wait for a better price when it
  //            is abundant. Measured -0.83 +- 0.11 on btcusd against +0.07 in
  //            the generator. Market orders reach only the best queue, so the
  //            rate is zero behind it.
  //
  // A CAVEAT THAT IS NOT SMALL: THESE SHAPES ARE NOT MEASURED HERE.
  //
  // Only the SCALES below are fitted. The three shapes are the paper's, taken
  // on its authority, and ten minutes per instrument cannot check them. The
  // slope against queue size at the touch, on the committed samples:
  //
  //                 ethusd          btcusd          xrpusd
  //     adds     +0.38 +- 0.35   +0.10 +- 0.24   +0.85 +- 0.25
  //     cancels  -0.26 +- 0.17   -1.03 +- 0.39   +0.18 +- 0.36
  //     trades      too few      -0.83 +- 0.11      too few
  //
  // The three instruments do not agree with each other on any row. Two of the
  // three cancel slopes are indistinguishable from flat and the third is
  // negative at 2.6 standard errors, so our data neither confirms the paper's
  // rising cancel rate nor refutes it -- it cannot see it. The trade slope
  // rests on 179 events on one instrument; the other two produced too few
  // trades to fit at all.
  //
  // So: the paper supplies the shapes, the captures supply the scales, and the
  // decay constants are the paper's qualitative curves with constants nobody
  // has fitted. The eight-hour captures are what would settle them. Nothing
  // downstream should be read as though these curves were measured here.
  struct Qr {
    static constexpr int kLevels = 4;    // modelled queues each side
    bool   enabled = false;

    // The queue axis. `aes` is the average event size the queue is counted in,
    // the same quantity apps/stats measures and writes beside every row.
    double aes = 240.0;                  // lots

    // THE SCALES ARE FITTED, THE SHAPES ARE THE PAPER'S.
    //
    // A birth-and-death queue's stationary distribution depends only on the
    // RATIO of arrival to departure, so the shape and the speed separate
    // cleanly: the ratios set the depth distribution, the overall scale sets
    // the event rate. Fitting exploits that -- search the two ratios per level
    // against the depth targets, then divide once for the rate -- and it means
    // no target was traded against another.
    //
    // Targets, from the ethusd ten-minute capture, per side, exposure-weighted:
    //
    //     level          0      1      2      3
    //     mean q      4.29   0.49   0.43   0.39
    //     P(q = 0)    0.00   0.79   0.80   0.85
    //     events/s    1.17   0.237  0.179  0.186
    //
    // and what these constants reproduce through the closed form:
    //
    //     mean q      4.29   0.49   0.43   0.39     exact by construction
    //     P(q = 0)    ----   0.793  0.803  0.847
    //     events/s    1.17   0.237  0.179  0.186    exact by construction
    //
    // THE RATES WERE ONCE TWICE THESE, and the cause is worth keeping. The
    // estimator accrues exposure for EVERY (side, level) on every step, so
    // exposure summed over both sides is twice the wall clock. The aggregation
    // that produced the targets divided that by two as well, which is the
    // correct wall clock and the wrong denominator for a per-side rate --
    // count and exposure must be summed over the same set. Every rate came out
    // doubled and the fit duly reproduced it. Nothing about the shapes moved:
    // a birth-and-death queue's law depends only on the ratios, so halving all
    // four scales at a level halves its event rate and leaves its distribution
    // untouched, which is why mean q and P(q=0) are unchanged above.
    //
    // P(q=0) is not a target at the touch and cannot be: level 0 here is the
    // BEST queue, so it is empty only when a whole side is, which the closed
    // form has no way to express. add_empty[0] is therefore not fitted -- it is
    // the paper's drop at an empty queue, 0.45 of the flat rate, and what it
    // governs in this implementation is how fast an emptied side refills.
    // Fitting it landed on 0.14 against an add rate of 2.9, a twentyfold
    // slower refill that would have left the book one-sided.

    // lambda^L(q) = q == 0 ? add_empty : add_rate * exp(-add_decay * (q - 1))
    double add_rate  [kLevels] = {0.6507, 0.5261, 0.3988, 0.6263};  // per second, per side
    double add_empty [kLevels] = {0.2928, 0.0316, 0.0279, 0.0188};
    double add_decay [kLevels] = {0.000,  0.120,  0.140,  0.150};   // flat at the touch

    // lambda^C(q) = cancel_rate * q / (q + cancel_half),  zero at q = 0
    double cancel_rate[kLevels] = {1.1715, 1.1394, 0.9407, 1.1708};
    double cancel_half[kLevels] = {2.500,  2.000,  2.000,  2.000};  // in AES

    // lambda^M(q) = trade_rate * exp(-trade_decay * (q - 1)),  zero at q = 0
    // and zero behind the touch: a market order takes the best queue.
    //
    // trade_rate is fitted, to the share of removals at the touch that are
    // trades rather than cancels: 0.1019 / (1.338 + 0.1019) = 7.08% measured,
    // 7.09% reproduced. It was 0.0657 before, inherited from a ratio between
    // two numbers this author had picked by hand -- which is not a fit, and was
    // not one when it was first written down as though it were.
    //
    // trade_decay is NOT fitted. It is the paper's exponential decay with a
    // constant nobody has measured. What it produces here is a log-log slope of
    // -0.42 +- 0.06 against btcusd's -0.83 +- 0.11: the sign is right, and the
    // sign was backwards before Model I, while the steepness is not settled by
    // 179 trade events on one instrument.
    double trade_rate  = 0.0926;
    double trade_decay = 0.240;

    // MARKET ORDER SIZE, as a multiple of AES, by sixteenth of the distribution.
    //
    // Every market order used to be exactly one AES, and that is wrong at both
    // ends. Pooled over the three committed captures, 427 prints:
    //
    //     quantile   0.25   0.50   0.75   0.90   0.99    max
    //     AES       0.009  0.067  0.196  0.790  3.118   5.23
    //
    // A typical market order is a fifteenth of an average event, not one, and
    // 8.4% of them are larger than one. Emitting a constant AES is therefore
    // both too big most of the time and incapable of ever being big -- which is
    // why NO trade ever reached past the touch, against 7.2% of volume on
    // ethusd and 32.4% on btcusd, and why level_ratio came out zero. Model II-a
    // does not fix that and was never going to: a market order arriving at Q_2
    // when Q_1 is empty is at the BEST price, so it is at the touch, not past
    // it. Only size gets past a queue.
    //
    // This is the measured distribution rather than a family fitted to it. A
    // lognormal matched the middle and then put the 99th percentile at 25 AES
    // against an observed 3.1, because the log standard deviation needed for
    // the lower tail makes the upper one absurd. Sixteen quantile edges,
    // sampled by inverse CDF with linear interpolation, reproduce the quartiles
    // exactly, the 90th at 0.82 against 0.79, and the share above one AES at
    // 7.4% against 8.4%.
    //
    // The top edge is the largest print in 427, so the last sixteenth has one
    // observation's worth of resolution and the sampled 99th comes out at 4.6
    // against 3.1. That is the tail being thin, not the model being wrong, and
    // it is the first thing the eight-hour captures would tighten.
    static constexpr int kSizeBins = 16;
    double trade_size_aes[kSizeBins + 1] = {
        0.0000, 0.0005, 0.0011, 0.0045, 0.0091, 0.0129, 0.0217, 0.0293, 0.0672,
        0.0916, 0.0916, 0.1006, 0.1960, 0.4506, 0.6253, 1.0825, 5.2305};

    // ---- Model II-b: the touch also watches the OPPOSITE queue -------------
    //
    // Huang et al. make the intensities at Q_1 functions of the target queue
    // and of S_{m,l}(q_-1), the opposite best queue in four regimes: empty,
    // small (<= m), usual (<= l), large (> l), with m and l its 33% and 67%
    // quantiles conditional on being positive. Measured on the captures those
    // are 2 and 6 AES (ethusd), 2 and 4 (btcusd), 1 and 3 (xrpusd).
    //
    // The paper notes this is one parameterisation among equivalents -- "one
    // can consider them as functions of the first level bid/ask imbalance" --
    // which is why this is the piece that matters here: imbalance is the MDP's
    // own state variable, so this is the mechanism that puts a signal in it.
    //
    // MEASURED AT THE TOUCH, per side, and the paper's two findings hold:
    //
    //     opposite queue          small    usual    large
    //     ethusd  adds/s          0.664    0.391    0.256     falling
    //             cancels/s       0.967    0.526    0.433
    //             trades/s        0.039    0.067    0.055     rising
    //     btcusd  adds/s          0.868    0.424    0.371     falling
    //             cancels/s       3.568    0.717    0.682
    //             trades/s        0.189    0.175    0.379     rising
    //     xrpusd  adds/s          0.428    0.335    0.760     RISING
    //             cancels/s       0.777    0.993    1.916
    //             trades/s        0.066    0.166    0.308     rising
    //
    // Limit insertion falls with the opposite queue on the two large-tick
    // instruments, which is the paper's finding and its reason: a thick
    // opposite side puts the efficient price nearer it, so quoting this side is
    // profitable. Market orders rise with it on all three -- transactions at
    // the target queue are cheap when its price is temporarily closer to the
    // efficient price -- which is the same reasoning read from the taker's end.
    //
    // xrpusd inverts the add and cancel rows, and it is the small-tick
    // instrument, the same split Model II-a showed. Dayri and Rosenbaum's tick
    // regimes again; these numbers are the large-tick ones.
    //
    // The multipliers are each regime's rate over the exposure-weighted mean of
    // the three, so applying them leaves the marginal alone PROVIDED the
    // simulator spends the same share of time in each regime as the capture did
    // (ethusd: 40.6 / 20.6 / 38.7 per cent). It does not exactly, so the
    // marginal drifts a little; that is measured after the fact rather than
    // assumed away.
    //
    // An empty opposite queue is never observed in the captures -- both sides
    // have a touch throughout -- so it borrows the "small" multiplier. The
    // paper says market orders are MORE frequent against an empty opposite
    // queue than a small one, since the target is then two ticks nearer the
    // reference price than the other side. That is not reproduced here because
    // nothing measured it.
    int    opp_m = 2, opp_l = 5;      // regime edges, in AES
    double opp_add   [3] = {1.478, 0.871, 0.570};   // small, usual, large
    double opp_cancel[3] = {1.446, 0.787, 0.648};
    double opp_trade [3] = {0.761, 1.325, 1.080};

    // Orders the price has walked away from.
    //
    // The model describes kLevels queues either side of p_ref. A price move
    // relabels every queue, and an order that falls past the outermost one
    // becomes invisible to it: never counted in a queue, never eligible for
    // cancellation, resting for ever. Measured, the book grew from 101 to 408
    // orders over two million events and was still climbing -- a wall of stale
    // depth behind the touch that nothing in the model can remove.
    //
    // The paper never meets this because it simulates K queues and no book
    // behind them; when p_ref moves it shifts the queues and redraws the
    // outermost from its invariant measure, so nothing is ever stranded. We
    // have a real order book, so the strays are real orders.
    //
    // They cancel independently, each on its own clock, at the rate the
    // outermost modelled queue cancels a queue holding one average event:
    // cancel_rate[K-1] / (1 + cancel_half[K-1]). That is derived from the
    // fitted constants rather than being a new free parameter, and it is the
    // paper's own K = 3 finding applied outward -- Q_4 and Q_5 behave like
    // Q_3, so an order past the window is treated as one at the edge of it.
    [[nodiscard]] double far_cancel_per_order() const noexcept {
      return cancel_rate[kLevels - 1] / (1.0 + cancel_half[kLevels - 1]);
    }

    // Model III: the chance the reference price follows an emptied best queue.
    // The paper calibrates this against the ten-minute volatility and the
    // mean-reversion ratio; that is gap 5 of docs/06 and is NOT done here. One
    // is the paper's own "purely order book driven" setting, where every
    // emptied queue moves the price and the volatility that results is the
    // maximal mechanical volatility -- which the paper finds is 5 bps against
    // an empirical 14, so this cannot be the whole story and is not claimed to
    // be.
    double theta = 1.0;
  };
  Qr qr;

  // The calibrated process with Model I flow instead of fixed weights. The
  // clock is not set here: under Model I the interevent time is an exponential
  // draw at the total rate, so mean_gap_ns does not apply and the event rate is
  // whatever the intensities add up to.
  [[nodiscard]] static FlowConfig ethusd_queue_reactive() noexcept {
    FlowConfig c = ethusd();
    c.qr.enabled = true;
    return c;
  }

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

    if (cfg_.qr.enabled) return next_queue_reactive(e);

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

  // ---- Model I ------------------------------------------------------------
  // Where a queue sits. ANCHORED TO THE REFERENCE PRICE, not to the touch, and
  // that is not a detail -- it is the difference between a model and a book
  // whose spread can only ever widen.
  //
  // The first version indexed levels from the current best price. Every add
  // then landed at or behind whatever the touch happened to be, so nothing ever
  // arrived INSIDE the spread, and once the spread opened there was no
  // mechanism to close it again. Measured: median spread 4 ticks against
  // ethusd's 1, at one tick 1% of the time against 91%, and the mid moving in
  // 0.4% of epochs against 2%. tools/mdp_params.py refused the process outright
  // -- "NOT USABLE for the MDP: the mid essentially never moves" -- which is
  // exactly right and is why that check exists.
  //
  // Huang et al. index Q_i at i - 0.5 ticks from p_ref, so Q_1 on each side is
  // the half-tick either side of it: a limit order arriving at an empty Q_1 IS
  // an order inside the spread, and the paper names that as one of the three
  // events that move p_ref. On an integer-tick book the same thing is mid_ for
  // the bid side and mid_ + 1 for the ask, one tick apart, which is the spread
  // these instruments sit at.
  //
  // mid_ IS p_ref here. It already carries the exogenous drift and the informed
  // impact, so the reference price has those two sources plus the endogenous
  // one below, which is what the paper's Model III adds to Model I.
  [[nodiscard]] Ticks queue_price(int side, int level) const noexcept {
    return (side == 0) ? mid_ - static_cast<Ticks>(level)
                       : mid_ + 1 + static_cast<Ticks>(level);
  }

  // Depth and order count at each modelled queue, read off what the generator
  // believes is resting. O(live_) per event, which is nothing on a book of the
  // size this path runs at -- the calibrated process holds about twelve orders
  // -- and much harder to get wrong than an incrementally maintained index.
  struct QueueState {
    Qty qty[2][FlowConfig::Qr::kLevels];
    int n[2][FlowConfig::Qr::kLevels];
    int far[2];        // resting outside the modelled window, per side
  };
  [[nodiscard]] QueueState queue_state() const noexcept {
    QueueState st{};
    for (const Live& l : live_) {
      const int side = (l.side == Side::Bid) ? 0 : 1;
      const Ticks anchor = queue_price(side, 0);
      const Ticks d = (side == 0) ? (anchor - l.price) : (l.price - anchor);
      // A negative distance is an order on the wrong side of p_ref, which a
      // price move can produce. It is as stranded as a far one and is counted
      // with them rather than dropped.
      if (d < 0 || d >= FlowConfig::Qr::kLevels) { ++st.far[side]; continue; }
      st.qty[side][d] += l.qty;
      ++st.n[side][d];
    }
    return st;
  }

  // Model III's endogenous price move: when a best queue is empty the reference
  // price follows it, with probability theta. The paper triggers this on the
  // three events that can empty a best queue or put an order inside the spread;
  // checking the state after each event is the same condition, reached from the
  // state rather than from the event that caused it, and it cannot miss one.
  //
  // Only one side can pull at a time. If both best queues are empty the book
  // has no touch at all and moving either way is arbitrary, so nothing moves
  // and the add flow refills them.
  void reference_price_step(const QueueState& st) noexcept {
    const bool bid_empty = st.n[0][0] == 0, ask_empty = st.n[1][0] == 0;
    if (bid_empty == ask_empty) return;             // both, or neither
    if (uniform() >= cfg_.qr.theta) return;
    mid_ += bid_empty ? -1 : 1;                     // the price follows the gap
  }


  // q = ceil(depth / aes), the paper's axis and the estimator's. Zero is an
  // empty queue and is its own state, not a small one.
  [[nodiscard]] int q_of(Qty depth) const noexcept {
    if (depth <= 0) return 0;
    const double a = cfg_.qr.aes > 0.0 ? cfg_.qr.aes : 1.0;
    const int q = static_cast<int>(std::ceil(static_cast<double>(depth) / a));
    return q < 1 ? 1 : q;
  }

  // Which regime the opposite queue is in, as Model II-b's S_{m,l}. Returns an
  // index into the three multipliers; an empty opposite queue borrows "small",
  // for the reason given beside those constants.
  [[nodiscard]] int opp_regime(const QueueState& st, int side) const noexcept {
    const int other = 1 - side;
    int best = -1;
    for (int l = 0; l < FlowConfig::Qr::kLevels && best < 0; ++l)
      if (st.n[other][l] > 0) best = l;
    if (best < 0) return 0;                       // nothing on the other side
    const int q = q_of(st.qty[other][best]);
    if (q <= cfg_.qr.opp_m) return 0;             // empty and small together
    if (q <= cfg_.qr.opp_l) return 1;
    return 2;
  }

  [[nodiscard]] double lambda_add(int lvl, int q) const noexcept {
    const FlowConfig::Qr& k = cfg_.qr;
    if (q <= 0) return k.add_empty[lvl];
    return k.add_rate[lvl] * std::exp(-k.add_decay[lvl] * static_cast<double>(q - 1));
  }
  [[nodiscard]] double lambda_cancel(int lvl, int q) const noexcept {
    const FlowConfig::Qr& k = cfg_.qr;
    if (q <= 0) return 0.0;
    const double x = static_cast<double>(q);
    return k.cancel_rate[lvl] * x / (x + k.cancel_half[lvl]);
  }
  // Model II-a's market order routing. A market order takes the BEST OFFER,
  // which is the first non-empty queue on that side and is not always Q_1: the
  // paper is explicit that "market orders can arrive at Q2 only if Q1 = 0 (that
  // is when Q2 is the best offer queue)", with "the shape of the intensity very
  // similar to the one obtained in the case of Q1".
  //
  // So the rate is the same function of the queue's own size wherever the best
  // offer happens to be. Figure 2's much smaller value at Q_2 is the
  // UNCONDITIONAL rate -- averaged over all the time Q_1 is occupied and no
  // market order can reach Q_2 at all -- and reading it as a conditional one
  // would double-count the emptiness.
  [[nodiscard]] double lambda_trade(int lvl, int q, int best) const noexcept {
    const FlowConfig::Qr& k = cfg_.qr;
    if (lvl != best || q <= 0) return 0.0;
    return k.trade_rate * std::exp(-k.trade_decay * static_cast<double>(q - 1));
  }

  // One step of the jump process: total rate, one event in proportion to its
  // own rate, then the clock advanced by an exponential draw at that rate. The
  // interevent distribution is a consequence here rather than a setting, which
  // is the point -- mean_gap_ns does not apply on this path.
  BookEvent next_queue_reactive(BookEvent e) noexcept {
    constexpr int kL = FlowConfig::Qr::kLevels;
    QueueState st = queue_state();
    reference_price_step(st);
    if (mid_ != ref_seen_) { st = queue_state(); ref_seen_ = mid_; }

    double rate[2][kL][3], far_rate[2];
    double total = 0.0;
    for (int s = 0; s < 2; ++s) {
      // The best offer on this side: the first queue with anything in it. -1
      // when the whole modelled window is empty, and then nothing trades.
      int best = -1;
      for (int l = 0; l < kL && best < 0; ++l)
        if (st.n[s][l] > 0) best = l;
      // Model II-b applies at the TOUCH only, which is where the paper applies
      // it: the intensities at Q_1 depend on the opposite queue, those behind
      // it do not.
      const int reg = opp_regime(st, s);
      const double ma = cfg_.qr.opp_add[reg], mc = cfg_.qr.opp_cancel[reg],
                   mt = cfg_.qr.opp_trade[reg];
      for (int l = 0; l < kL; ++l) {
        const int q = q_of(st.qty[s][l]);
        const bool touch = (l == 0);
        rate[s][l][0] = lambda_add(l, q) * (touch ? ma : 1.0);
        rate[s][l][1] = st.n[s][l] > 0 ? lambda_cancel(l, q) * (touch ? mc : 1.0) : 0.0;
        rate[s][l][2] = st.n[s][l] > 0 ? lambda_trade(l, q, best) * (touch ? mt : 1.0) : 0.0;
        total += rate[s][l][0] + rate[s][l][1] + rate[s][l][2];
      }
      // Independent per-order cancellation outside the window, so a stray
      // cannot rest for ever. Proportional to the count, which is what bounds
      // the pile: a fixed rate would not.
      far_rate[s] = static_cast<double>(st.far[s]) * cfg_.qr.far_cancel_per_order();
      total += far_rate[s];
    }

    // Every rate zero is possible only with an empty book and add_empty all
    // zero, which is a misconfiguration rather than a state. Emit an add at the
    // touch so the stream does not stall -- qr_add, not make_add, because
    // make_add is the fixed-weight path and would place at a level this model
    // never chose and with a size it never uses.
    if (!(total > 0.0)) { ts_ += 1; return qr_add(e, Side::Bid, 0); }

    // Exponential interarrival at the total rate. uniform() is [0,1); guard the
    // zero so the logarithm cannot be -inf.
    const double u = uniform();
    const double gap_s = -std::log(u > 0.0 ? u : 1e-18) / total;
    ts_ += static_cast<Nanos>(gap_s * 1e9) + 1;

    double r = uniform() * total;
    for (int s = 0; s < 2; ++s) {
      const Side side = (s == 0) ? Side::Bid : Side::Ask;
      for (int l = 0; l < kL; ++l)
        for (int t = 0; t < 3; ++t) {
          r -= rate[s][l][t];
          if (r > 0.0) continue;
          if (t == 0) return qr_add(e, side, l);
          if (t == 1) return qr_cancel(e, s, l);
          return qr_trade(e, side);
        }
      r -= far_rate[s];
      if (r <= 0.0) return qr_cancel_far(e, s);
    }
    return qr_add(e, Side::Bid, 0);   // unreachable barring a rounding edge
  }

  // A constant order size at each limit, which is the paper's assumption and
  // the reason its queue axis is in average event sizes at all.
  BookEvent qr_add(BookEvent e, Side side, int lvl) noexcept {
    e.type     = EventType::Add;
    e.side     = side;
    e.qty      = static_cast<Qty>(cfg_.qr.aes > 1.0 ? cfg_.qr.aes : 1.0);
    e.order_id = next_id_++;
    // No crossing guard is needed and none is wanted. Bid levels sit at mid_
    // and below, ask levels at mid_ + 1 and above, so the two sides cannot
    // meet by construction. A guard here would silently relocate orders the
    // model placed deliberately, which is how the level distribution stopped
    // being the one that was fitted last time.
    e.price = queue_price(side == Side::Bid ? 0 : 1, lvl);
    return e;
  }

  // A cancellation at a named queue, on one of the orders actually standing in
  // it. Uniform WITHIN the queue, which is Assumption 3 of the paper; uniform
  // over the whole book, which is what the fixed-weight path does, is what made
  // cancel intensity proportional to the order count for no reason.
  BookEvent qr_cancel(BookEvent e, int side, int lvl) noexcept {
    const Ticks touch = queue_price(side, 0);
    int n = 0;
    for (const Live& l : live_) {
      if (((l.side == Side::Bid) ? 0 : 1) != side) continue;
      const Ticks d = (side == 0) ? (touch - l.price) : (l.price - touch);
      if (d == static_cast<Ticks>(lvl)) ++n;
    }
    if (n == 0) return qr_add(e, side == 0 ? Side::Bid : Side::Ask, lvl);
    int k = static_cast<int>(rng_() % static_cast<std::uint64_t>(n));
    for (const Live& l : live_) {
      if (((l.side == Side::Bid) ? 0 : 1) != side) continue;
      const Ticks d = (side == 0) ? (touch - l.price) : (l.price - touch);
      if (d != static_cast<Ticks>(lvl)) continue;
      if (k-- > 0) continue;
      e.type     = EventType::Delete;
      e.order_id = l.id;
      e.side     = l.side;
      e.price    = l.price;
      e.qty      = 0;
      return e;
    }
    return qr_add(e, side == 0 ? Side::Bid : Side::Ask, lvl);
  }

  // Cancel one stray: an order this side of the book that the price has walked
  // past, so no modelled queue contains it. Uniform among them, for the same
  // reason cancellation is uniform within a queue.
  BookEvent qr_cancel_far(BookEvent e, int side) noexcept {
    const Ticks anchor = queue_price(side, 0);
    auto stranded = [&](const Live& l) {
      if (((l.side == Side::Bid) ? 0 : 1) != side) return false;
      const Ticks d = (side == 0) ? (anchor - l.price) : (l.price - anchor);
      return d < 0 || d >= static_cast<Ticks>(FlowConfig::Qr::kLevels);
    };
    int n = 0;
    for (const Live& l : live_) n += stranded(l) ? 1 : 0;
    if (n == 0) return qr_add(e, side == 0 ? Side::Bid : Side::Ask, 0);
    int k = static_cast<int>(rng_() % static_cast<std::uint64_t>(n));
    for (const Live& l : live_) {
      if (!stranded(l) || k-- > 0) continue;
      e.type     = EventType::Delete;
      e.order_id = l.id;
      e.side     = l.side;
      e.price    = l.price;
      e.qty      = 0;
      return e;
    }
    return qr_add(e, side == 0 ? Side::Bid : Side::Ask, 0);
  }

  // A market order's size, in lots, from the measured distribution: pick a
  // sixteenth uniformly and interpolate inside it. At least one lot, because a
  // market order for nothing is not an event and the book would reject it.
  [[nodiscard]] Qty draw_trade_size() noexcept {
    const FlowConfig::Qr& k = cfg_.qr;
    const double u = uniform() * FlowConfig::Qr::kSizeBins;
    int i = static_cast<int>(u);
    double f = u - static_cast<double>(i);
    if (i >= FlowConfig::Qr::kSizeBins) { i = FlowConfig::Qr::kSizeBins - 1; f = 1.0; }
    const double aes_mult = k.trade_size_aes[i]
                          + f * (k.trade_size_aes[i + 1] - k.trade_size_aes[i]);
    const double lots = aes_mult * (k.aes > 0.0 ? k.aes : 1.0);
    return lots < 1.0 ? Qty{1} : static_cast<Qty>(lots);
  }

  // A market order into the named side's best queue. It goes through the
  // matching engine like any other aggressive order, so it consumes the front
  // of the queue and a fill means queue position actually mattered.
  BookEvent qr_trade(BookEvent e, Side resting) noexcept {
    e.type     = EventType::Aggress;
    // A Bid aggressor buys and lifts the ask, so taking the BID queue needs an
    // Ask aggressor. Getting this backwards is what mislabelled every synthetic
    // print once already.
    e.side     = (resting == Side::Bid) ? Side::Ask : Side::Bid;
    e.order_id = next_id_++;
    e.price    = 0;
    e.qty      = draw_trade_size();
    // The Glosten-Milgrom split is orthogonal to where the order came from, so
    // it is applied here exactly as make_aggress applies it: a share of takers
    // are informed and the mid follows them. Duplicating the three lines rather
    // than sharing them would let the two paths drift apart, which is the one
    // thing this whole file keeps being bitten by.
    if (uniform() < cfg_.informed_frac) {
      ++informed_;
      if (uniform() < cfg_.informed_impact_prob)
        pending_impact_ = (e.side == Side::Bid) ? cfg_.informed_impact : -cfg_.informed_impact;
    } else {
      ++uninformed_;
    }
    return e;
  }

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
  Ticks             ref_seen_ = 0;
  bool              has_bid_ = false;
  bool              has_ask_ = false;
  Ticks             best_bid_ = 0;
  Ticks             best_ask_ = 0;
  OrderId           next_id_ = 1;
  SeqNum            seq_     = 1;
  Nanos             ts_      = 0;
};

}  // namespace lob
