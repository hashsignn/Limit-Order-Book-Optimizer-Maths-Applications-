// Reads a capture file line by line: plain text, gzip, or stdin.
//
// Captures are written gzipped because a 10-minute btcusd session is ~20 MB raw
// and compresses about 5:1, and because the recorder must never be tempted to
// drop a field to save space. Reading them should not require a decompression
// step the user has to remember, so gzip is handled here when zlib is present
// and the alternative is spelled out when it is not.
#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace lob {

class LineReader {
 public:
  LineReader() = default;
  ~LineReader();
  LineReader(const LineReader&)            = delete;
  LineReader& operator=(const LineReader&) = delete;

  // `path` may be "-" for stdin. A name ending in .gz is decompressed if this
  // build has zlib; if it does not, open() fails with a message saying exactly
  // what to pipe instead rather than reading compressed bytes as text.
  [[nodiscard]] bool open(const char* path);

  // Yields the next line without its newline. The view is valid until the next
  // call. Returns false at end of input.
  //
  // A final line with no trailing newline is returned normally: an interrupted
  // recording ends mid-frame, and that partial line is a decode failure to be
  // counted, not a file the reader should refuse.
  [[nodiscard]] bool next(std::string_view* line);

  [[nodiscard]] const std::string& error() const noexcept { return error_; }
  [[nodiscard]] std::size_t bytes_read() const noexcept { return bytes_; }
  [[nodiscard]] static bool have_gzip() noexcept;

 private:
  [[nodiscard]] bool fill();

  void*       handle_ = nullptr;   // FILE* or gzFile, per gz_
  bool        gz_     = false;
  bool        eof_    = false;
  bool        owns_   = false;
  std::string buf_;
  std::size_t pos_    = 0;
  std::size_t end_    = 0;
  std::size_t bytes_  = 0;
  std::string error_;
};

}  // namespace lob
