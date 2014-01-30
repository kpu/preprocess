#include "util/fake_ofstream.hh"
#include "util/file_piece.hh"

#include <iostream>
#include <numeric>

#include <stdint.h>
#include <string.h>
#include <unicode/uchar.h>
#include <unicode/uscript.h>

struct SelectLatin {
  bool operator()(const StringPiece &line) const {
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

template <class Pass> int FilterParallel(const Pass &pass, int argc, char **argv) {
  uint64_t input = 0, output = 0;
  if (argc == 1) {
    StringPiece line;
    util::FilePiece in(0);
    util::FakeOFStream out(1);
    while (true) {
      try {
        line = in.ReadLine();
      } catch (const util::EndOfFileException &e) { break; }
      ++input;
      if (pass(line)) {
        out << line << '\n';
        ++output;
      }
    }
  } else if (argc == 5) {
    StringPiece line0, line1;
    util::FilePiece in0(argv[1], &std::cerr), in1(argv[2]);
    util::FakeOFStream out0(util::CreateOrThrow(argv[3])), out1(util::CreateOrThrow(argv[4]));
    while (true) {
      try {
        line0 = in0.ReadLine();
      } catch (const util::EndOfFileException &e) { break; }
      line1 = in1.ReadLine();
      ++input;
      if (pass(line0) && pass(line1)) {
        out0 << line0 << '\n';
        out1 << line1 << '\n';
        ++output;
      }
    }
    try {
      line1 = in1.ReadLine();
      std::cerr << "Input is not balaced: " << argv[2] << " has " << line1 << std::endl;
      return 2;
    } catch (const util::EndOfFileException &e) {}
  } else {
    std::cerr << 
      "To filter one file, run\n" << argv[0] << " <stdin >stdout\n"
      "To filter parallel files, run\n" << argv[0] << "in0 in1 out0 out1\n";
    return 1;
  }
  std::cerr << "Kept " << input << " / " << output << " = " << (static_cast<float>(output) / static_cast<float>(input)) << std::endl;
  return 0;
}

int main(int argc, char *argv[]) {
  return FilterParallel(SelectLatin(), argc, argv);
}
