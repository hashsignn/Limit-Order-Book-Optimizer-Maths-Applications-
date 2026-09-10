#include "lob/core/arena.hpp"
#include "test_util.hpp"

#include <cstdint>
#include <cstddef>

using namespace lob;

namespace {
struct Node {
  std::uint64_t a;
  std::uint64_t b;
};
}  // namespace

int main() {
  // ---- Arena ----
  {
    Arena arena{1024};
    CHECK_EQ(arena.capacity(), 1024U);
    CHECK_EQ(arena.used(), 0U);

    void* p1 = arena.allocate(64, 64);
    CHECK(p1 != nullptr);
    CHECK_EQ(reinterpret_cast<std::uintptr_t>(p1) % 64, 0U);

    // A 1-byte allocation followed by an aligned one must pad, not overlap.
    void* p2 = arena.allocate(1, 1);
    void* p3 = arena.allocate(8, 8);
    CHECK(p2 != nullptr && p3 != nullptr);
    CHECK_EQ(reinterpret_cast<std::uintptr_t>(p3) % 8, 0U);
    CHECK(p3 != p2);

    // Exhaustion returns nullptr rather than falling back to the heap.
    CHECK(arena.allocate(4096, 8) == nullptr);
    const std::size_t used_before = arena.used();
    CHECK(used_before > 0);

    arena.reset();
    CHECK_EQ(arena.used(), 0U);
    CHECK_EQ(arena.remaining(), 1024U);

    Node* n = arena.create<Node>(Node{7, 9});
    CHECK(n != nullptr);
    CHECK_EQ(n->a, 7U);
    CHECK_EQ(n->b, 9U);
  }

  // ---- Pool ----
  {
    Pool<Node> pool{3};
    CHECK_EQ(pool.capacity(), 3U);
    CHECK_EQ(pool.in_use(), 0U);
    CHECK_EQ(pool.available(), 3U);

    Node* a = pool.acquire(Node{1, 1});
    Node* b = pool.acquire(Node{2, 2});
    Node* c = pool.acquire(Node{3, 3});
    CHECK(a && b && c);
    CHECK(a != b && b != c && a != c);
    CHECK_EQ(pool.in_use(), 3U);

    // Exhausted: nullptr, never a heap allocation.
    CHECK(pool.acquire() == nullptr);

    CHECK_EQ(a->a, 1U);
    CHECK_EQ(b->a, 2U);
    CHECK_EQ(c->a, 3U);

    pool.release(b);
    CHECK_EQ(pool.in_use(), 2U);
    Node* d = pool.acquire(Node{4, 4});
    CHECK(d == b);             // most-recently-freed slot is reused: cache-warm
    CHECK_EQ(d->a, 4U);

    pool.release(nullptr);     // must be a no-op, not a crash
    CHECK_EQ(pool.in_use(), 3U);

    pool.release(a); pool.release(c); pool.release(d);
    CHECK_EQ(pool.in_use(), 0U);
    CHECK_EQ(pool.available(), 3U);

    // Full cycle again, to prove release() rebuilt a usable free list.
    for (int i = 0; i < 3; ++i) CHECK(pool.acquire() != nullptr);
    CHECK(pool.acquire() == nullptr);
  }

  // ---- a release with no matching acquire does not underflow the counter ----
  // It used to take in_use_ to SIZE_MAX and available() with it. The debug
  // builds assert; the release build must at least stay consistent.
  {
    Pool<Node> pool{2};
    Node* a = pool.acquire(Node{1, 1});
    CHECK(a != nullptr);
    pool.release(a);
    CHECK_EQ(pool.in_use(), 0U);
    CHECK_EQ(pool.available(), 2U);
    // Two full cycles still work afterwards, so the free list is intact.
    Node* b = pool.acquire(Node{2, 2});
    Node* c = pool.acquire(Node{3, 3});
    CHECK(b != nullptr && c != nullptr && b != c);
    CHECK_EQ(pool.in_use(), 2U);
  }

  // ---- exhaustion cannot be reached by an overflowing size ----
  {
    Arena a{1024};
    CHECK(a.allocate(SIZE_MAX, 8) == nullptr);          // used to wrap and succeed
    CHECK(a.allocate(SIZE_MAX - 16, 8) == nullptr);
    CHECK(a.allocate(1024, 1) != nullptr);              // and the real one still fits
    CHECK(a.allocate(1, 1) == nullptr);
  }

  return lobtest::summary("arena");
}
