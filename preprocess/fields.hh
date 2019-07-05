#pragma once

#include "util/string_piece.hh"
#include "util/murmur_hash.hh"

#include <algorithm>
#include <limits>
#include <vector>

namespace preprocess {

// [begin, end) as is the custom of our people.
struct FieldRange {
  // Note that end can be the maximum integer.
  unsigned int begin, end;
  bool operator<(const FieldRange &other) const {
    return begin < other.begin;
  }
  static const unsigned int kInfiniteEnd = std::numeric_limits<unsigned int>::max();
};

// Parse the cut-style 1-3,9,12- representation of fields.
void ParseFields(const char *arg, std::vector<FieldRange> &indices);

// Sort and combine field ranges into smaller ones.
void DefragmentFields(std::vector<FieldRange> &indices);

// Do a callback with each individual field that was selected.
template <class Functor> inline void IndividualFields(StringPiece str, const std::vector<FieldRange> &indices, char delim, Functor &callback) {
  const char *begin = str.data();
  const char *const end = str.data() + str.size();
  unsigned int index = 0;
  for (const FieldRange f : indices) {
    for (; index < f.begin; ++index) {
      begin = std::find(begin, end, delim) + 1;
      if (begin >= end) return;
    }
    for (; index < f.end; ++index) {
      const char *found = std::find(begin, end, delim);
      callback(StringPiece(begin, found - begin));
      begin = found + 1;
      if (begin >= end) return;
    }
  }
  return;
}

// Do a callback with ranges of fields.
template <class Functor> inline void RangeFields(StringPiece str, const std::vector<FieldRange> &indices, char delim, Functor &callback) {
  const char *begin = str.data();
  const char *const end = str.data() + str.size();
  unsigned int index = 0;
  for (const FieldRange f : indices) {
    for (; index < f.begin; ++index) {
      begin = std::find(begin, end, delim) + 1;
      if (begin >= end) return;
    }
    if (f.end == FieldRange::kInfiniteEnd) {
      callback(StringPiece(begin, end - begin));
      return;
    }
    const char *old_begin = begin;
    for (; index < f.end; ++index) {
      const char *found = std::find(begin, end, delim);
      begin = found + 1;
      if (begin >= end) {
        callback(StringPiece(old_begin, end - old_begin));
        return;
      }
    }
    callback(StringPiece(old_begin, begin - old_begin - 1));
  }
  return;
}

// This is called with the parts of the input that relate to the key.
class HashCallback {
  public:
    explicit HashCallback(uint64_t seed = 47849374332489ULL) : hash_(seed) /* Be different from deduper */ {}

    void operator()(StringPiece key) {
      hash_ = util::MurmurHashNative(key.data(), key.size(), hash_);
    }

    uint64_t Hash() const { return hash_; }

  private:
    uint64_t hash_;
};

} // namespace preprocess
