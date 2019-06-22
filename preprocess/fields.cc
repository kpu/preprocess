#include "preprocess/fields.hh"
#include "util/exception.hh"

#include <stdlib.h>

#include <algorithm>

namespace preprocess {

namespace {
unsigned int ConsumeInt(const char *&arg) {
  char *end;
  unsigned int ret = strtoul(arg, &end, 10);
  UTIL_THROW_IF(end == arg, util::Exception, "Expected field " << arg << " to begin with a number.");
  arg = end;
  return ret;
}
} // namespace

void ParseFields(const char *arg, std::vector<FieldRange> &indices) {
  FieldRange add;
  while (*arg) {
    if (*arg == '-') {
      add.begin = 0;
    } else {
      // -1 because cut is 1-indexed.
      add.begin = ConsumeInt(arg) - 1;
    }
    switch (*arg) {
      case ',': case 0:
        add.end = add.begin + 1;
        break;
      case '-':
        ++arg;
        if (*arg == 0 || *arg == ',') {
          // 5-
          add.end = FieldRange::kInfiniteEnd;
        } else {
          // 5-6
          add.end = ConsumeInt(arg);
          UTIL_THROW_IF(add.end <= add.begin, util::Exception, "Empty range [" << add.begin << ", " << add.end << ")");
        }
        break;
      default:
        UTIL_THROW(util::Exception, "Expected , - or string end after number in " << arg);
    }
    // Swallow ,
    if (*arg == ',') {
      ++arg;
    }
    indices.push_back(add);
  }
}

void DefragmentFields(std::vector<FieldRange> &indices) {
  std::sort(indices.begin(), indices.end());
  for (unsigned int i = 1; i < indices.size();) {
    UTIL_THROW_IF(indices[i-1].end > indices[i].begin, util::Exception, "Overlapping index ranges");
    if (indices[i-1].end == indices[i].begin) {
      indices[i-1].end = indices[i].end;
      indices.erase(indices.begin() + i);
    } else {
      ++i;
    }
  }
}

} // namespace preprocess
