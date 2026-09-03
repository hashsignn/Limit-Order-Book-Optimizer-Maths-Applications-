// Bump allocator and object pool.
//
// The hot path must not call malloc. Both types here allocate their storage
// once, up front, and hand out memory with a pointer bump or a free-list pop.
// Neither ever frees to the OS while running.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <new>
#include <type_traits>

namespace lob {

inline constexpr std::size_t kCacheLine = 64;
// Two cache lines. x86 prefetches in 128-byte pairs and Apple silicon uses
// 128-byte lines, so 64 is not enough to guarantee no false sharing.
inline constexpr std::size_t kFalseSharingAlign = 128;

// Monotonic bump allocator over one owned block. Reset rewinds the whole
// arena at once; there is no individual free by design.
class Arena {
 public:
  explicit Arena(std::size_t bytes)
      : size_(bytes),
        base_(static_cast<std::byte*>(::operator new(bytes, std::align_val_t{kCacheLine}))) {}

  ~Arena() { ::operator delete(base_, std::align_val_t{kCacheLine}); }

  Arena(const Arena&)            = delete;
  Arena& operator=(const Arena&) = delete;
  Arena(Arena&&)                 = delete;
  Arena& operator=(Arena&&)      = delete;

  // Returns nullptr when exhausted. Callers on the hot path must treat that as
  // a hard error and account for it, never silently fall back to the heap.
  [[nodiscard]] void* allocate(std::size_t bytes, std::size_t align) noexcept {
    const std::size_t cur     = reinterpret_cast<std::uintptr_t>(base_ + used_);
    const std::size_t aligned = (cur + align - 1) & ~(align - 1);
    const std::size_t pad     = aligned - cur;
    if (used_ + pad + bytes > size_) return nullptr;
    used_ += pad + bytes;
    return base_ + used_ - bytes;
  }

  template <typename T, typename... Args>
  [[nodiscard]] T* create(Args&&... args) noexcept(std::is_nothrow_constructible_v<T, Args...>) {
    void* p = allocate(sizeof(T), alignof(T));
    return p ? new (p) T(static_cast<Args&&>(args)...) : nullptr;
  }

  void reset() noexcept { used_ = 0; }

  [[nodiscard]] std::size_t used() const noexcept      { return used_; }
  [[nodiscard]] std::size_t capacity() const noexcept  { return size_; }
  [[nodiscard]] std::size_t remaining() const noexcept { return size_ - used_; }

 private:
  std::size_t size_;
  std::byte*  base_;
  std::size_t used_ = 0;
};

// Fixed-capacity pool of T with an intrusive free list threaded through the
// unused slots, so it costs no extra memory. acquire/release are O(1) and
// allocation-free.
template <typename T>
class Pool {
  static_assert(std::is_trivially_destructible_v<T>,
                "Pool does not run destructors; use a trivially destructible T");
  static_assert(sizeof(T) >= sizeof(void*), "T must be large enough to hold a free-list link");

 public:
  explicit Pool(std::size_t capacity)
      : capacity_(capacity),
        storage_(static_cast<Slot*>(::operator new(capacity * sizeof(Slot),
                                                   std::align_val_t{alignof(Slot)}))) {
    // Thread the free list through every slot, front to back, so the first
    // allocations walk memory in order and prefetch well.
    for (std::size_t i = 0; i + 1 < capacity_; ++i) storage_[i].next = &storage_[i + 1];
    if (capacity_ > 0) storage_[capacity_ - 1].next = nullptr;
    free_ = capacity_ > 0 ? &storage_[0] : nullptr;
  }

  ~Pool() { ::operator delete(storage_, std::align_val_t{alignof(Slot)}); }

  Pool(const Pool&)            = delete;
  Pool& operator=(const Pool&) = delete;
  Pool(Pool&&)                 = delete;
  Pool& operator=(Pool&&)      = delete;

  template <typename... Args>
  [[nodiscard]] T* acquire(Args&&... args) noexcept {
    if (free_ == nullptr) return nullptr;
    Slot* s = free_;
    free_   = s->next;
    ++in_use_;
    return new (static_cast<void*>(&s->value)) T(static_cast<Args&&>(args)...);
  }

  void release(T* p) noexcept {
    if (p == nullptr) return;
    auto* s = reinterpret_cast<Slot*>(p);
    s->next = free_;
    free_   = s;
    --in_use_;
  }

  [[nodiscard]] std::size_t in_use() const noexcept    { return in_use_; }
  [[nodiscard]] std::size_t capacity() const noexcept  { return capacity_; }
  [[nodiscard]] std::size_t available() const noexcept { return capacity_ - in_use_; }

 private:
  union Slot {
    Slot* next;
    T     value;
    Slot() noexcept : next(nullptr) {}
    ~Slot() {}
  };

  std::size_t capacity_;
  Slot*       storage_;
  Slot*       free_   = nullptr;
  std::size_t in_use_ = 0;
};

}  // namespace lob
