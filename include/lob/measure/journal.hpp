// Append-only binary journal.
//
// Every decision the system makes is written here with its sequence number and
// timestamps. Replaying the journal through the same binaries must reproduce
// the same output, byte for byte — that property is what makes a backtest
// measure the system you actually run, and what makes a live incident
// debuggable after the fact.
//
// Records are fixed-size PODs. The header stores the record size and a hash of
// the type name, so opening a journal with the wrong record type fails loudly
// instead of returning garbage.
#pragma once

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace lob {

inline constexpr std::uint32_t kJournalMagic   = 0x4C4F424AU;  // "LOBJ"
inline constexpr std::uint16_t kJournalVersion = 1;

struct JournalHeader {
  std::uint32_t magic       = kJournalMagic;
  std::uint16_t version     = kJournalVersion;
  std::uint16_t record_size = 0;
  std::uint64_t type_hash   = 0;
  std::uint64_t reserved    = 0;
};
static_assert(sizeof(JournalHeader) == 24);

// FNV-1a over the type name: cheap, and only needs to catch accidental mismatch.
[[nodiscard]] constexpr std::uint64_t type_hash(std::string_view name) noexcept {
  std::uint64_t h = 1469598103934665603ULL;
  for (const char c : name) {
    h ^= static_cast<std::uint64_t>(static_cast<unsigned char>(c));
    h *= 1099511628211ULL;
  }
  return h;
}

std::FILE* journal_open_write(const std::string& path, const JournalHeader& hdr);
std::FILE* journal_open_read(const std::string& path, JournalHeader& hdr_out);

// Buffered writer. Records accumulate in a preallocated buffer and go out in
// batches, so the writing thread does not make a syscall per record.
template <typename Record>
class JournalWriter {
  static_assert(std::is_trivially_copyable_v<Record>, "journal records must be trivially copyable");
  static_assert(sizeof(Record) <= 65535, "record too large for the 16-bit size field");

 public:
  JournalWriter(const std::string& path, std::string_view type_name,
                std::size_t batch_records = 4096)
      // Clamped, because append() is noexcept and indexes this buffer directly:
      // batch_records = 0 made `buf_[n_++] = r` a heap write past the end on the
      // very first record.
      : buf_(batch_records ? batch_records : 1) {
    JournalHeader hdr;
    hdr.record_size = static_cast<std::uint16_t>(sizeof(Record));
    hdr.type_hash   = type_hash(type_name);
    f_ = journal_open_write(path, hdr);
  }

  ~JournalWriter() { close(); }

  JournalWriter(const JournalWriter&)            = delete;
  JournalWriter& operator=(const JournalWriter&) = delete;

  void append(const Record& r) noexcept {
    buf_[n_++] = r;
    ++total_;
    if (n_ == buf_.size()) flush();
  }

  // WRITE ERRORS ARE STICKY, NOT DISCARDED.
  //
  // This used to be `std::fwrite(...)` with the return value dropped, and
  // close() dropped fclose()'s too. A full disk, a quota or a closed pipe
  // silently truncated the journal and the writer reported success. This file's
  // own header promises that replaying a journal reproduces the same output
  // "byte for byte", and a journal quietly missing its last 4,096 records is
  // worse than no journal, because it is trusted.
  //
  // A destructor cannot throw, so the failure is recorded rather than raised and
  // callers check failed() after close(). src/policy_table.cpp already gets the
  // fclose half of this right; this is the same rule.
  void flush() noexcept {
    if (n_ == 0 || f_ == nullptr) return;
    if (std::fwrite(buf_.data(), sizeof(Record), n_, f_) != n_) failed_ = true;
    n_ = 0;
  }

  void close() noexcept {
    if (f_ == nullptr) return;
    flush();
    // Closing is where deferred write errors surface: the data may still be in
    // the C library's buffer when fwrite returns.
    if (std::fclose(f_) != 0) failed_ = true;
    f_ = nullptr;
  }

  [[nodiscard]] std::uint64_t written() const noexcept { return total_; }
  // True if any write or the close failed. Check it after close(); a journal
  // that reports this has fewer records on disk than written() counted.
  [[nodiscard]] bool failed() const noexcept { return failed_; }

 private:
  std::vector<Record> buf_;
  std::size_t         n_     = 0;
  std::uint64_t       total_  = 0;
  bool                failed_ = false;
  std::FILE*          f_      = nullptr;
};

// Reads the whole journal into memory. Fine for the sizes Phase 0 deals with;
// a streaming or mmap reader is a Phase 1 concern.
template <typename Record>
[[nodiscard]] std::vector<Record> journal_read_all(const std::string& path,
                                                   std::string_view type_name) {
  JournalHeader hdr{};
  // Owning handle: push_back below can throw bad_alloc on a large journal, and
  // an explicit fclose after it would never run.
  const std::unique_ptr<std::FILE, int (*)(std::FILE*)> f{journal_open_read(path, hdr),
                                                          &std::fclose};
  if (hdr.record_size != sizeof(Record))
    throw std::runtime_error("journal record size mismatch: " + path);
  if (hdr.type_hash != type_hash(type_name))
    throw std::runtime_error("journal record type mismatch: " + path);

  std::vector<Record> out;
  Record r{};
  while (std::fread(&r, sizeof(Record), 1, f.get()) == 1) out.push_back(r);
  return out;
}

}  // namespace lob
