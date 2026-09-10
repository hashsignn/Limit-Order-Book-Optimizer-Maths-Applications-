#include "lob/measure/journal.hpp"
#include "lob/core/types.hpp"
#include "test_util.hpp"

#include <cstdio>
#include <cstring>
#include <string>

using namespace lob;

namespace {

// A decision record: enough to replay what the system saw and what it chose.
struct Decision {
  SeqNum        seq;
  std::uint64_t ts_recv_ticks;
  std::uint64_t ts_decide_ticks;
  Ticks         bid;
  Ticks         ask;
  Qty           bid_qty;
  Qty           ask_qty;
  std::int64_t  inventory;
};
static_assert(std::is_trivially_copyable_v<Decision>);

struct Other { std::uint64_t a; std::uint64_t b; std::uint64_t c;
               std::uint64_t d; std::uint64_t e; std::uint64_t f;
               std::uint64_t g; std::uint64_t h; };
static_assert(sizeof(Other) == sizeof(Decision), "same size, different type: hash must catch it");

}  // namespace

int main() {
  const std::string path = "test_journal.tmp";
  std::remove(path.c_str());

  constexpr int kN = 10'000;

  // Write more records than the batch size, so buffer wrap-around is exercised.
  {
    JournalWriter<Decision> w{path, "Decision", 256};
    for (int i = 0; i < kN; ++i) {
      w.append(Decision{static_cast<SeqNum>(i), static_cast<std::uint64_t>(i) * 3,
                        static_cast<std::uint64_t>(i) * 3 + 7,
                        10'000 + i, 10'002 + i, 5, 5, i % 11 - 5});
    }
  }  // destructor flushes and closes

  // written() counts what append() was handed. It returned 0 for the life of
  // this class -- total_ was declared, read by written(), and incremented
  // nowhere -- and nothing asserted it, which is why.
  {
    const std::string p2 = "test_journal_count.tmp";
    JournalWriter<Decision> w{p2, "Decision", 64};
    for (int i = 0; i < 500; ++i) w.append(Decision{static_cast<SeqNum>(i), 1, 2, 3, 4, 5, 6, 7});
    CHECK_EQ(w.written(), 500U);
    w.close();
    CHECK(!w.failed());
    CHECK_EQ(journal_read_all<Decision>(p2, "Decision").size(), 500U);
    std::remove(p2.c_str());
  }

  // A batch size of zero used to make `buf_[n_++] = r` a heap write past the
  // end of an empty vector, on the first record, in a noexcept function.
  {
    const std::string p3 = "test_journal_zerobatch.tmp";
    JournalWriter<Decision> w{p3, "Decision", 0};
    for (int i = 0; i < 10; ++i) w.append(Decision{static_cast<SeqNum>(i), 1, 2, 3, 4, 5, 6, 7});
    w.close();
    CHECK(!w.failed());
    CHECK_EQ(journal_read_all<Decision>(p3, "Decision").size(), 10U);
    std::remove(p3.c_str());
  }

  // A write that cannot land must be reported, not swallowed. /dev/full accepts
  // an open and fails every write, which is exactly the full-disk case.
  {
    JournalWriter<Decision> w{"/dev/full", "Decision", 4};
    for (int i = 0; i < 64; ++i) w.append(Decision{static_cast<SeqNum>(i), 1, 2, 3, 4, 5, 6, 7});
    w.close();
    ::lobtest::report(w.failed(), "a failed write is reported", __FILE__, __LINE__,
                      "wrote " + std::to_string(w.written()) + " records to /dev/full");
  }

  const auto back = journal_read_all<Decision>(path, "Decision");
  CHECK_EQ(back.size(), static_cast<std::size_t>(kN));

  bool all_match = true;
  for (int i = 0; i < kN; ++i) {
    const Decision& d = back[static_cast<std::size_t>(i)];
    if (d.seq != static_cast<SeqNum>(i) || d.bid != 10'000 + i || d.ask != 10'002 + i ||
        d.ts_decide_ticks != static_cast<std::uint64_t>(i) * 3 + 7 ||
        d.inventory != i % 11 - 5) { all_match = false; break; }
  }
  CHECK(all_match);

  // Opening with the wrong record type must fail loudly, even though the types
  // are the same size — this is the bug the type hash exists to catch.
  CHECK_THROWS(journal_read_all<Other>(path, "Other"));

  // Not a journal at all.
  {
    std::FILE* f = std::fopen("not_a_journal.tmp", "wb");
    const char junk[64] = "definitely not a journal header";
    std::fwrite(junk, 1, sizeof(junk), f);
    std::fclose(f);
    CHECK_THROWS(journal_read_all<Decision>("not_a_journal.tmp", "Decision"));
    std::remove("not_a_journal.tmp");
  }

  // Missing file.
  CHECK_THROWS(journal_read_all<Decision>("no_such_file.tmp", "Decision"));

  // Empty journal: valid header, zero records.
  {
    const std::string empty = "empty_journal.tmp";
    { JournalWriter<Decision> w{empty, "Decision"}; }
    CHECK_EQ(journal_read_all<Decision>(empty, "Decision").size(), 0U);
    std::remove(empty.c_str());
  }

  // Determinism: the same records written twice produce identical bytes.
  {
    const std::string p1 = "det1.tmp", p2 = "det2.tmp";
    for (const auto& p : {p1, p2}) {
      JournalWriter<Decision> w{p, "Decision", 64};
      for (int i = 0; i < 500; ++i)
        w.append(Decision{static_cast<SeqNum>(i), 1, 2, 3 + i, 4 + i, 5, 6, 7});
    }
    const auto a = journal_read_all<Decision>(p1, "Decision");
    const auto b = journal_read_all<Decision>(p2, "Decision");
    CHECK_EQ(a.size(), b.size());
    CHECK(std::memcmp(a.data(), b.data(), a.size() * sizeof(Decision)) == 0);
    std::remove(p1.c_str()); std::remove(p2.c_str());
  }

  std::remove(path.c_str());
  return lobtest::summary("journal");
}
