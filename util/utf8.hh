/* Utilities for Unicode that don't depend on ICU. */

#ifndef UTIL_UTF8
#define UTIL_UTF8

#include "util/exception.hh"
#include "util/string_piece.hh"

#include <cstddef>
#include <cstdint>

#include <exception>
#include <string>

namespace util {

// This is what happens when you pass bad UTF8.  
class NotUTF8Exception : public std::exception {
  public:
    explicit NotUTF8Exception(const StringPiece &original) throw();

    ~NotUTF8Exception() throw();

    const char *what() const throw() { return "Bad UTF-8"; }
};

/* The following has been modified from SentencePiece */
// Copyright 2016 Google Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.!

typedef char32_t char32;
typedef uint32_t uint32;

// Return (x & 0xC0) == 0x80;
// Since trail bytes are always in [0x80, 0xBF], we can optimize:
inline bool IsTrailByte(char x) { return static_cast<signed char>(x) < -0x40; }

inline bool IsValidCodepoint(char32 c) {
  return (static_cast<uint32>(c) < 0xD800) || (c >= 0xE000 && c <= 0x10FFFF);
}

constexpr uint32 kUnicodeError = 0xFFFD;

// Presumes end > begin.
inline char32 DecodeUTF8(const char *begin, const char *end, size_t *mblen) {
  const size_t len = end - begin;

  if (static_cast<unsigned char>(begin[0]) < 0x80) {
    *mblen = 1;
    return static_cast<unsigned char>(begin[0]);
  } else if (len >= 2 && (begin[0] & 0xE0) == 0xC0) {
    const char32 cp = (((begin[0] & 0x1F) << 6) | ((begin[1] & 0x3F)));
    if (IsTrailByte(begin[1]) && cp >= 0x0080 && IsValidCodepoint(cp)) {
      *mblen = 2;
      return cp;
    }
  } else if (len >= 3 && (begin[0] & 0xF0) == 0xE0) {
    const char32 cp = (((begin[0] & 0x0F) << 12) | ((begin[1] & 0x3F) << 6) |
                       ((begin[2] & 0x3F)));
    if (IsTrailByte(begin[1]) && IsTrailByte(begin[2]) && cp >= 0x0800 &&
        IsValidCodepoint(cp)) {
      *mblen = 3;
      return cp;
    }
  } else if (len >= 4 && (begin[0] & 0xf8) == 0xF0) {
    const char32 cp = (((begin[0] & 0x07) << 18) | ((begin[1] & 0x3F) << 12) |
                       ((begin[2] & 0x3F) << 6) | ((begin[3] & 0x3F)));
    if (IsTrailByte(begin[1]) && IsTrailByte(begin[2]) &&
        IsTrailByte(begin[3]) && cp >= 0x10000 && IsValidCodepoint(cp)) {
      *mblen = 4;
      return cp;
    }
  }

  // Invalid UTF-8.
  *mblen = 1;
  throw NotUTF8Exception(StringPiece(begin, end - begin));
}

/* End modified version of SentencePiece */

class DecodeUTF8Iterator {
  public:
    DecodeUTF8Iterator() : current_codepoint_(kUnicodeError) {}

    explicit DecodeUTF8Iterator(StringPiece from) : remaining_(from), current_(from.data(), 0) {
      ++*this;
    }

    DecodeUTF8Iterator &operator++() {
      remaining_.remove_prefix(current_.size());
      if (!remaining_.empty()) {
        size_t length;
        current_codepoint_ = DecodeUTF8(remaining_.begin(), remaining_.end(), &length);
        current_ = StringPiece(remaining_.data(), length);
      } else {
        current_codepoint_ = kUnicodeError;
      }
      return *this;
    }
    DecodeUTF8Iterator operator++(int) {
      DecodeUTF8Iterator ret(*this);
      ++*this;
      return ret;
    }

    // Comparison is defined if they come from the same sequence or are value-initialized.
    bool operator==(const DecodeUTF8Iterator &other) const {
      return remaining_.data() == other.remaining_.data();
    }

    char32_t operator*() const { return current_codepoint_; }

    char32_t UTF32() const { return current_codepoint_; }

    StringPiece UTF8() const { return current_; }

    operator bool() const { return !remaining_.empty(); }

  private:
    // All the string, including current_.
    StringPiece remaining_;
    // Just the current codepoint in utf8.
    StringPiece current_;
    // Current codepoint in utf32.
    char32_t current_codepoint_;
};

// for (char32_t codepoint : DecodeUTF8Range(str)) {}
class DecodeUTF8Range {
  public:
    DecodeUTF8Range(StringPiece str)
      : begin_(str), end_(StringPiece(str.data() + str.size(), 0)) {}

    DecodeUTF8Iterator begin() const { return begin_; }
    DecodeUTF8Iterator end() const { return end_; }

  private:
    const DecodeUTF8Iterator begin_, end_;
};

bool IsUTF8(const StringPiece &text);

} // namespace util

#endif // UTIL_UTF8
