// Minimal JSON scanning, for feed decode only.
//
// This is not a general JSON library, deliberately. Capture files are
// machine-generated with a fixed shape, and decoding happens once per capture
// rather than on the hot path, so what is wanted is a scanner that pulls known
// scalars out of a known layout without allocating — not a DOM.
//
// The input is UNTRUSTED: it arrived over a public websocket and was written to
// disk unmodified. Every function here is total and bounds-checked. Malformed
// input yields false or an empty view; nothing reads past the end, nothing
// throws, nothing allocates. fuzz/fuzz_bitstamp.cpp exists to keep that true.
//
// Numbers are read as exact decimal strings rather than through double. That is
// the point rather than a limitation: a price that survives a round trip
// through binary floating point only most of the time is a price that makes a
// backtest irreproducible. See parse_decimal.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace lob::json {

using View = std::string_view;

// ---- lexing ---------------------------------------------------------------

[[nodiscard]] constexpr bool is_ws(char c) noexcept {
  return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

constexpr void skip_ws(View s, std::size_t& i) noexcept {
  while (i < s.size() && is_ws(s[i])) ++i;
}

// Advances `i` past one complete string literal, starting ON the opening quote.
// Handles backslash escapes so that a brace, bracket or quote inside a string
// never terminates a structure. Returns false if the string is unterminated.
[[nodiscard]] constexpr bool skip_string(View s, std::size_t& i) noexcept {
  if (i >= s.size() || s[i] != '"') return false;
  ++i;
  while (i < s.size()) {
    const char c = s[i];
    if (c == '\\') {
      // A trailing backslash must not let the loop skip past the end.
      if (i + 1 >= s.size()) return false;
      i += 2;
      continue;
    }
    ++i;
    if (c == '"') return true;
  }
  return false;
}

// Advances `i` past exactly one JSON value of any type. Nesting is tracked with
// an explicit counter rather than recursion, so a hostile input made of ten
// thousand open brackets costs a loop iteration each instead of a stack frame.
[[nodiscard]] constexpr bool skip_value(View s, std::size_t& i) noexcept {
  skip_ws(s, i);
  if (i >= s.size()) return false;

  if (s[i] == '"') return skip_string(s, i);

  if (s[i] == '{' || s[i] == '[') {
    std::size_t depth = 0;
    while (i < s.size()) {
      const char c = s[i];
      if (c == '"') {
        if (!skip_string(s, i)) return false;
        continue;
      }
      if (c == '{' || c == '[') ++depth;
      else if (c == '}' || c == ']') {
        if (depth == 0) return false;
        --depth;
        ++i;
        if (depth == 0) return true;
        continue;
      }
      ++i;
    }
    return false;   // unbalanced
  }

  // Number, true, false, null: run to the next structural character.
  const std::size_t start = i;
  while (i < s.size() && s[i] != ',' && s[i] != '}' && s[i] != ']' && !is_ws(s[i])) ++i;
  return i > start;
}

// ---- lookup ---------------------------------------------------------------

// Value of `key` in the object `s`, which must begin at its '{'. Returns the
// RAW value: quotes still on a string, braces still on an object. Empty view if
// the key is absent or the object is malformed.
//
// Only the object's own keys are searched. Nested values are skipped wholesale
// rather than descended into, so a key inside `data` never satisfies a lookup
// meant for the envelope — which matters here, because Bitstamp puts `price`
// inside `data` and `channel` outside it, and confusing the two levels is
// exactly the bug this shape prevents.
[[nodiscard]] constexpr View find(View s, View key) noexcept {
  std::size_t i = 0;
  skip_ws(s, i);
  if (i >= s.size() || s[i] != '{') return {};
  ++i;

  while (true) {
    skip_ws(s, i);
    if (i >= s.size()) return {};
    if (s[i] == '}') return {};
    if (s[i] == ',') { ++i; continue; }

    const std::size_t key_start = i;
    if (!skip_string(s, i)) return {};
    // key_start..i spans the quoted key; compare the inside.
    const View k = s.substr(key_start + 1, i - key_start - 2);

    skip_ws(s, i);
    if (i >= s.size() || s[i] != ':') return {};
    ++i;
    skip_ws(s, i);

    const std::size_t val_start = i;
    if (!skip_value(s, i)) return {};
    if (k == key) return s.substr(val_start, i - val_start);
  }
}

// As find(), with the surrounding quotes removed when the value is a string.
// A non-string value is returned unchanged, so a field that is sometimes
// quoted and sometimes not reads the same either way.
[[nodiscard]] constexpr View find_scalar(View s, View key) noexcept {
  const View v = find(s, key);
  if (v.size() >= 2 && v.front() == '"' && v.back() == '"') return v.substr(1, v.size() - 2);
  return v;
}

// Iterates a JSON array. `arr` must include its brackets. Call with i = 0 and
// keep calling until it returns false; each call sets `elem` to the raw text of
// the next element. Nested arrays come back whole, brackets included, so a
// snapshot row ["price","amount","id"] is itself iterable.
[[nodiscard]] constexpr bool array_next(View arr, std::size_t& i, View* elem) noexcept {
  if (elem == nullptr) return false;
  if (i == 0) {
    skip_ws(arr, i);
    if (i >= arr.size() || arr[i] != '[') return false;
    ++i;
  }
  skip_ws(arr, i);
  if (i >= arr.size() || arr[i] == ']') return false;
  if (arr[i] == ',') { ++i; skip_ws(arr, i); }
  if (i >= arr.size() || arr[i] == ']') return false;

  const std::size_t start = i;
  if (!skip_value(arr, i)) return false;
  *elem = arr.substr(start, i - start);
  if (elem->size() >= 2 && elem->front() == '"' && elem->back() == '"')
    *elem = elem->substr(1, elem->size() - 2);
  return true;
}

// ---- numbers --------------------------------------------------------------

[[nodiscard]] constexpr bool parse_u64(View s, std::uint64_t* out) noexcept {
  if (s.empty() || out == nullptr) return false;
  std::uint64_t v = 0;
  for (const char c : s) {
    if (c < '0' || c > '9') return false;
    const auto d = static_cast<std::uint64_t>(c - '0');
    if (v > (UINT64_MAX - d) / 10U) return false;      // overflow
    v = v * 10U + d;
  }
  *out = v;
  return true;
}

// Parses a decimal string into an integer scaled by 10^`scale`.
//
//     "50123.45", scale 2  ->  5012345
//     "0.50000000", scale 8 ->   50000000
//
// Exact or nothing. If the value carries more significant fractional digits
// than `scale` can hold, this returns false rather than rounding: a decoder
// that quietly truncates size or price is a decoder that produces a book which
// disagrees with the exchange by an amount nobody ever sees. Trailing zeros
// beyond `scale` are not significant and are accepted.
//
// Exponent notation is rejected outright. Bitstamp's `_str` fields never use
// it, and accepting it here would mean silently mis-scaling the one field
// (`amount`, for a dust order) where a JSON encoder might emit 1e-08.
[[nodiscard]] constexpr bool parse_decimal(View s, unsigned scale, std::int64_t* out) noexcept {
  if (s.empty() || out == nullptr || scale > 18) return false;

  std::size_t i = 0;
  bool neg = false;
  if (s[i] == '-') { neg = true; ++i; }
  else if (s[i] == '+') { ++i; }

  std::int64_t v = 0;
  bool any_digit = false;
  constexpr std::int64_t kMax = INT64_MAX;

  // Integer part.
  for (; i < s.size() && s[i] >= '0' && s[i] <= '9'; ++i) {
    const auto d = static_cast<std::int64_t>(s[i] - '0');
    if (v > (kMax - d) / 10) return false;
    v = v * 10 + d;
    any_digit = true;
  }

  unsigned used = 0;
  if (i < s.size() && s[i] == '.') {
    ++i;
    for (; i < s.size() && s[i] >= '0' && s[i] <= '9'; ++i) {
      const auto d = static_cast<std::int64_t>(s[i] - '0');
      if (used < scale) {
        if (v > (kMax - d) / 10) return false;
        v = v * 10 + d;
        ++used;
      } else if (s[i] != '0') {
        return false;                       // precision we cannot represent
      }
      any_digit = true;
    }
  }

  if (i != s.size()) return false;           // trailing junk, or 'e'
  if (!any_digit) return false;

  // Pad out the digits the string did not supply.
  for (; used < scale; ++used) {
    if (v > kMax / 10) return false;
    v *= 10;
  }

  *out = neg ? -v : v;
  return true;
}

}  // namespace lob::json
