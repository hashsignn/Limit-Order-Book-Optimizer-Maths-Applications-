#include "lob/measure/journal.hpp"

namespace lob {

std::FILE* journal_open_write(const std::string& path, const JournalHeader& hdr) {
  std::FILE* f = std::fopen(path.c_str(), "wbe");
  if (f == nullptr) throw std::runtime_error("cannot open journal for writing: " + path);
  if (std::fwrite(&hdr, sizeof(hdr), 1, f) != 1) {
    std::fclose(f);
    throw std::runtime_error("cannot write journal header: " + path);
  }
  return f;
}

std::FILE* journal_open_read(const std::string& path, JournalHeader& hdr_out) {
  std::FILE* f = std::fopen(path.c_str(), "rbe");
  if (f == nullptr) throw std::runtime_error("cannot open journal for reading: " + path);
  if (std::fread(&hdr_out, sizeof(hdr_out), 1, f) != 1) {
    std::fclose(f);
    throw std::runtime_error("truncated journal header: " + path);
  }
  if (hdr_out.magic != kJournalMagic) {
    std::fclose(f);
    throw std::runtime_error("not a journal file: " + path);
  }
  if (hdr_out.version != kJournalVersion) {
    std::fclose(f);
    throw std::runtime_error("unsupported journal version: " + path);
  }
  return f;
}

}  // namespace lob
