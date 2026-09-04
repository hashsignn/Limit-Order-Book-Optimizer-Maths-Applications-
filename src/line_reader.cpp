#include "lob/feed/line_reader.hpp"

#include <cstdio>
#include <cstring>

#if defined(LOB_HAVE_ZLIB)
#include <zlib.h>
#endif

namespace lob {

namespace {
constexpr std::size_t kChunk = 1 << 16;

bool ends_with(const char* s, const char* suffix) {
  const std::size_t n = std::strlen(s), m = std::strlen(suffix);
  return n >= m && std::strcmp(s + n - m, suffix) == 0;
}
}  // namespace

bool LineReader::have_gzip() noexcept {
#if defined(LOB_HAVE_ZLIB)
  return true;
#else
  return false;
#endif
}

LineReader::~LineReader() {
  if (handle_ == nullptr || !owns_) return;
#if defined(LOB_HAVE_ZLIB)
  if (gz_) { gzclose(static_cast<gzFile>(handle_)); return; }
#endif
  std::fclose(static_cast<std::FILE*>(handle_));
}

bool LineReader::open(const char* path) {
  buf_.resize(kChunk);
  pos_ = end_ = 0;
  eof_ = false;

  if (std::strcmp(path, "-") == 0) {
    handle_ = stdin;
    owns_   = false;
    gz_     = false;
    return true;
  }

  if (ends_with(path, ".gz")) {
#if defined(LOB_HAVE_ZLIB)
    gzFile f = gzopen(path, "rb");
    if (f == nullptr) { error_ = std::string("cannot open ") + path; return false; }
    handle_ = f;
    owns_   = true;
    gz_     = true;
    return true;
#else
    error_ = std::string("this build has no zlib, so ") + path +
             " cannot be read directly.\n"
             "       Pipe it instead:  gunzip -c " + path + " | replay --bitstamp -";
    return false;
#endif
  }

  std::FILE* f = std::fopen(path, "rb");
  if (f == nullptr) { error_ = std::string("cannot open ") + path; return false; }
  handle_ = f;
  owns_   = true;
  gz_     = false;
  return true;
}

bool LineReader::fill() {
  if (eof_) return false;

  // Move the partial line to the front, then top the buffer up. A line longer
  // than the buffer grows it rather than being split, because half a JSON
  // object decoded as a whole one is a silent wrong answer.
  const std::size_t keep = end_ - pos_;
  if (keep > 0) std::memmove(buf_.data(), buf_.data() + pos_, keep);
  pos_ = 0;
  end_ = keep;
  if (end_ + kChunk > buf_.size()) buf_.resize(end_ + kChunk);

  std::size_t got = 0;
#if defined(LOB_HAVE_ZLIB)
  if (gz_) {
    const int r = gzread(static_cast<gzFile>(handle_), buf_.data() + end_,
                         static_cast<unsigned>(kChunk));
    got = (r > 0) ? static_cast<std::size_t>(r) : 0;
  } else
#endif
  {
    got = std::fread(buf_.data() + end_, 1, kChunk, static_cast<std::FILE*>(handle_));
  }

  if (got == 0) { eof_ = true; return false; }
  end_   += got;
  bytes_ += got;
  return true;
}

bool LineReader::next(std::string_view* line) {
  if (line == nullptr) return false;
  while (true) {
    const void* nl = std::memchr(buf_.data() + pos_, '\n', end_ - pos_);
    if (nl != nullptr) {
      const auto* p = static_cast<const char*>(nl);
      const std::size_t len = static_cast<std::size_t>(p - (buf_.data() + pos_));
      *line = std::string_view(buf_.data() + pos_, len);
      pos_ += len + 1;
      return true;
    }
    if (!fill()) {
      // Whatever is left with no newline after it is the last line.
      if (end_ > pos_) {
        *line = std::string_view(buf_.data() + pos_, end_ - pos_);
        pos_  = end_;
        return true;
      }
      return false;
    }
  }
}

}  // namespace lob
