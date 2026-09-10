// Differential test for OrderMap against std::unordered_map.
//
// Backward-shift deletion is the subtlest code in this repository. When a slot
// is freed, any element that probed PAST that slot to reach its home has to be
// walked back, or it becomes unreachable — a lookup will hit the hole and stop.
// The failure mode is silent: no crash, no corruption the compiler can see,
// just an order that has vanished from the book's index while still sitting in
// a level's FIFO.
//
// Hand-written cases will not find that. What finds it is a long random
// sequence of inserts and erases compared, operation by operation, against a
// map that is obviously correct.
#include "lob/book/order_map.hpp"
#include "test_util.hpp"

#include <cstdint>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

using namespace lob;

int main() {
  // ---- basics ----
  {
    OrderMap m{16};
    CHECK_EQ(m.size(), 0U);
    CHECK(m.find(42) == OrderMap::kEmpty);
    CHECK(m.insert(42, 7));
    CHECK_EQ(m.find(42), 7U);
    CHECK_EQ(m.size(), 1U);
    CHECK(!m.insert(42, 9));                 // duplicate rejected
    CHECK_EQ(m.find(42), 7U);                // and the original survives
    CHECK(m.erase(42));
    CHECK(m.find(42) == OrderMap::kEmpty);
    CHECK(!m.erase(42));                     // erasing twice is not a crash
    CHECK_EQ(m.size(), 0U);
  }

  // ---- keys that collide by construction ----
  // Same home slot, so every one of them probes through the others. This is the
  // exact shape backward-shift deletion has to get right.
  {
    OrderMap m{64};
    const std::size_t cap = m.capacity();
    std::vector<OrderId> keys;
    // Find keys whose mixed hash lands on the same slot.
    for (OrderId k = 1; keys.size() < 8 && k < 2'000'000; ++k)
      if ((OrderMap::mix(k) & (cap - 1)) == 0) keys.push_back(k);
    ::lobtest::report(keys.size() == 8, "found colliding keys", __FILE__, __LINE__,
                      std::to_string(keys.size()) + " found");

    for (std::size_t i = 0; i < keys.size(); ++i)
      CHECK(m.insert(keys[i], static_cast<std::uint32_t>(i)));
    for (std::size_t i = 0; i < keys.size(); ++i)
      CHECK_EQ(m.find(keys[i]), static_cast<std::uint32_t>(i));

    // Erase from the MIDDLE of the probe chain: the elements behind it must
    // still be reachable afterwards.
    CHECK(m.erase(keys[3]));
    CHECK(m.find(keys[3]) == OrderMap::kEmpty);
    for (std::size_t i = 0; i < keys.size(); ++i) {
      if (i == 3) continue;
      ::lobtest::report(m.find(keys[i]) == static_cast<std::uint32_t>(i),
                        "survivor still reachable after mid-chain erase",
                        __FILE__, __LINE__, "i=" + std::to_string(i));
    }
    // Erase the HEAD of the chain: same requirement.
    CHECK(m.erase(keys[0]));
    for (std::size_t i = 1; i < keys.size(); ++i) {
      if (i == 3) continue;
      ::lobtest::report(m.find(keys[i]) == static_cast<std::uint32_t>(i),
                        "survivor still reachable after head erase",
                        __FILE__, __LINE__, "i=" + std::to_string(i));
    }
  }

  // ---- differential against std::unordered_map ----
  {
    constexpr std::size_t kCap = 4096;
    OrderMap                                   m{kCap};
    std::unordered_map<OrderId, std::uint32_t> ref;
    std::mt19937_64 rng{20260904};
    std::vector<OrderId> live;

    int mismatches = 0;
    for (int step = 0; step < 400'000; ++step) {
      // Keys drawn from a space only a few times the capacity, so collisions
      // and reinsertions of just-erased keys are constant rather than rare.
      const OrderId key = rng() % (kCap * 3);
      const auto    val = static_cast<std::uint32_t>(rng() & 0xFFFF);

      // Erase-heavy once the table is loaded, to keep it under capacity and to
      // exercise deletion far more than insertion.
      const bool do_erase = !live.empty() && (ref.size() > kCap / 2 || (rng() % 3) == 0);

      if (do_erase) {
        const OrderId k = live[rng() % live.size()];
        const bool a = m.erase(k);
        const bool b = ref.erase(k) > 0;
        if (a != b) { ++mismatches; break; }
        for (std::size_t i = 0; i < live.size(); ++i)
          if (live[i] == k) { live[i] = live.back(); live.pop_back(); break; }
      } else if (ref.size() < kCap - 1) {
        const bool a = m.insert(key, val);
        const bool b = ref.emplace(key, val).second;
        if (a != b) { ++mismatches; break; }
        if (a) live.push_back(key);
      }

      if (m.size() != ref.size()) { ++mismatches; break; }

      // Every 64 steps, verify the ENTIRE table agrees — including that keys
      // which should be absent really are.
      if ((step & 0x3F) == 0) {
        for (const auto& [k, v] : ref) {
          if (m.find(k) != v) { ++mismatches; break; }
        }
        for (int probe = 0; probe < 32; ++probe) {
          const OrderId k = rng() % (kCap * 3);
          const bool in_ref = ref.count(k) > 0;
          const bool in_m   = m.find(k) != OrderMap::kEmpty;
          if (in_ref != in_m) { ++mismatches; break; }
        }
      }
      if (mismatches) break;
    }
    ::lobtest::report(mismatches == 0, "OrderMap matches std::unordered_map",
                      __FILE__, __LINE__, std::to_string(mismatches) + " mismatches");
  }

  // ---- fill to capacity, drain completely, refill ----
  // A leaked slot or a stale entry shows up as an insert that should succeed
  // and does not.
  {
    OrderMap m{256};
    const std::size_t n = 200;
    for (OrderId k = 1; k <= n; ++k) CHECK(m.insert(k, static_cast<std::uint32_t>(k)));
    CHECK_EQ(m.size(), n);
    for (OrderId k = 1; k <= n; ++k) CHECK(m.erase(k));
    CHECK_EQ(m.size(), 0U);
    for (OrderId k = 1; k <= n; ++k)
      ::lobtest::report(m.find(k) == OrderMap::kEmpty, "drained clean", __FILE__, __LINE__,
                        "k=" + std::to_string(k));
    // Refill with different keys: if deletion left debris this fails.
    for (OrderId k = 1000; k < 1000 + n; ++k)
      ::lobtest::report(m.insert(k, 1), "refill after drain", __FILE__, __LINE__,
                        "k=" + std::to_string(k));
    CHECK_EQ(m.size(), n);
  }

  // ---- a full table is refused, not spun on ----
  // find(), insert() and erase_at() are unbounded probe loops. On a full table
  // an absent key never terminates -- a hang, not a crash. OrderBook prevents it
  // by sizing its order pool to half the map, but the map said so nowhere and
  // could not be used safely on its own.
  {
    OrderMap m{8};                       // capacity rounds to 16
    const std::size_t cap = m.capacity();
    for (OrderId k = 1; k <= cap; ++k)
      ::lobtest::report(m.insert(k, static_cast<std::uint32_t>(k)), "fills to capacity",
                        __FILE__, __LINE__, "k=" + std::to_string(k));
    CHECK_EQ(m.size(), cap);
    CHECK(!m.insert(99'999, 1));         // refused, and this call RETURNS
    CHECK_EQ(m.size(), cap);
    // Every key that went in is still findable on a completely full table.
    for (OrderId k = 1; k <= cap; ++k) CHECK_EQ(m.find(k), static_cast<std::uint32_t>(k));
    CHECK(m.erase(1));                   // and erasing frees a slot again
    CHECK(m.insert(99'999, 7));
    CHECK_EQ(m.find(99'999), 7U);
  }

  // ---- capacity is at least twice what was asked for, without overflowing ----
  // Tested through capacity_for() rather than by constructing, because the
  // overflow case describes a table nobody can allocate.
  {
    CHECK_EQ(OrderMap::capacity_for(0), 32U);
    CHECK_EQ(OrderMap::capacity_for(16), 32U);
    CHECK(OrderMap::capacity_for(1000) >= 2000U);
    CHECK(OrderMap::capacity_for(1 << 20) >= (2U << 20));
    // `expected_orders * 2` used to overflow here and leave the capacity at 16.
    CHECK(OrderMap::capacity_for((std::size_t{1} << 62) + 1) > 16U);
    CHECK(OrderMap::capacity_for(SIZE_MAX) > 16U);
    // And the constructed table agrees with the function.
    CHECK_EQ(OrderMap{1000}.capacity(), OrderMap::capacity_for(1000));
  }

  // ---- clear ----
  {
    OrderMap m{64};
    for (OrderId k = 1; k <= 20; ++k) (void)m.insert(k, 1);
    m.clear();
    CHECK_EQ(m.size(), 0U);
    for (OrderId k = 1; k <= 20; ++k) CHECK(m.find(k) == OrderMap::kEmpty);
    CHECK(m.insert(5, 9));
    CHECK_EQ(m.find(5), 9U);
  }

  return lobtest::summary("order_map");
}
