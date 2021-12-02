#include "preprocess/parallel.hh"
#include "util/file_stream.hh"
#include "util/file_piece.hh"

#include <iostream>
#include <numeric>
#include <string>
#include <vector>

#include <stdint.h>
#include <string.h>
#include <unicode/uchar.h>
#include <unicode/uscript.h>

struct SelectLatin {
  bool operator()(const util::StringPiece &line) const {
    int32_t offset = 0;
    int32_t length = static_cast<int32_t>(line.size());
    size_t counts[USCRIPT_CODE_LIMIT];
    memset(counts, 0, sizeof(counts));
    size_t angle = 0;
    while (offset < length) {
      UChar32 character;
      U8_NEXT(line.data(), offset, length, character);
      // Avoid bad unicode and control characters
      if (character < 32) return false;
      UErrorCode err = U_ZERO_ERROR;
      UScriptCode script = uscript_getScript(character, &err);
      if (U_FAILURE(err) || script == USCRIPT_INVALID_CODE) return false;
      ++counts[script];
      if (character == '<' || character == '>') ++angle;
    }
    float total = static_cast<float>(std::accumulate(counts, counts + USCRIPT_CODE_LIMIT, 0));
    if (static_cast<float>(counts[USCRIPT_LATIN] + counts[USCRIPT_INHERITED] + counts[USCRIPT_COMMON] - angle) < total * 0.9) return false;
    if (static_cast<float>(counts[USCRIPT_LATIN]) < total * 0.5) return false;
    return true;
  }
};

int main(int argc, char *argv[]) {
  std::vector<std::string> args;
  for (int i = 1; i < argc; ++i) {
    args.push_back(argv[i]);
  }
  return preprocess::FilterParallel<SelectLatin>(args);
}
