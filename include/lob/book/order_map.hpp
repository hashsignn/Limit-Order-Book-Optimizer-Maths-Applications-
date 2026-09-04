// Open-addressed order-id -> slot map with linear probing.
//
// std::unordered_map is a cache miss per lookup: it is a node-per-element
// structure and the book does one lookup for every cancel and execute, which is
// most of the feed. This is a flat array of {key, value}, so a hit is usually
// one cache line.
//
// Deletion uses backward-shift rather than tombstones. Tombstones accumulate
// over a trading day of balanced adds and cancels and quietly degrade every
// probe; backward-shift keeps the table exactly as dense as its live contents.
#pragma once

#include <cstdint>
#include <vector>

#include "lob/core/types.hpp"

namespace lob {

class OrderMap {
 public:
  static constexpr std::uint32_t kEmpty = 0xFFFFFFFFU;

  // Rounds up to a power of two with headroom, so the load factor stays under
  // ~0.5 and probe chains stay short.
  explicit OrderMap(std::size_t expected_orders) {
    std::size_t cap = 16;
    while (cap < expected_orders * 2) cap <<= 1;
    mask_ = cap - 1;
    slots_.assign(cap, Slot{});
  }

  // Splitmix64 finalizer. Exchange order ids are often sequential or blocked,
  // which linear probing handles badly without mixing.
  [[nodiscard]] static constexpr std::uint64_t mix(std::uint64_t x) noexcept {
    x ^= x >> 30; x *= 0xBF58476D1CE4E5B9ULL;
    x ^= x >> 27; x *= 0x94D049BB133111EBULL;
    x ^= x >> 31;
    return x;
  }

  [[nodiscard]] std::uint32_t find(OrderId key) const noexcept {
    std::size_t i = mix(key) & mask_;
    while (slots_[i].used) {
      if (slots_[i].key == key) return slots_[i].value;
      i = (i + 1) & mask_;
    }
    return kEmpty;
  }

  // Returns false if the key was already present (the caller treats that as a
  // duplicate-order feed error rather than overwriting).
  bool insert(OrderId key, std::uint32_t value) noexcept {
    std::size_t i = mix(key) & mask_;
    while (slots_[i].used) {
      if (slots_[i].key == key) return false;
      i = (i + 1) & mask_;
    }
    slots_[i] = Slot{key, value, true};
    ++size_;
    return true;
  }

  bool erase(OrderId key) noexcept {
    std::size_t i = mix(key) & mask_;
    while (slots_[i].used) {
      if (slots_[i].key == key) { erase_at(i); return true; }
      i = (i + 1) & mask_;
    }
    return false;
  }

  void clear() noexcept {
    for (auto& s : slots_) s.used = false;
    size_ = 0;
  }

  [[nodiscard]] std::size_t size() const noexcept     { return size_; }
  [[nodiscard]] std::size_t capacity() const noexcept { return slots_.size(); }

 private:
  struct Slot {
    OrderId       key   = 0;
    std::uint32_t value = 0;
    bool          used  = false;
  };

  // Backward-shift deletion. Walk forward from the hole; any element whose
  // ideal slot is not cyclically inside (hole, j] can move back into the hole
  // without breaking its own probe chain.
  void erase_at(std::size_t i) noexcept {
    std::size_t j = i;
    while (true) {
      slots_[i].used = false;
      std::size_t k = 0;
      while (true) {
        j = (j + 1) & mask_;
        if (!slots_[j].used) { --size_; return; }
        k = mix(slots_[j].key) & mask_;
        const bool blocked = (i <= j) ? (i < k && k <= j) : (i < k || k <= j);
        if (!blocked) break;
      }
      slots_[i] = slots_[j];
      i = j;
    }
  }

  std::vector<Slot> slots_;
  std::size_t       mask_ = 0;
  std::size_t       size_ = 0;
};

}  // namespace lob
