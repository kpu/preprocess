#include <string.h>
#include "util/file_piece.hh"
#include "util/file_stream.hh"
#include "util/tokenize_piece.hh"
#include "preprocess/base64.hh"

#include <algorithm>
#include <string>

int main(int argc, char *argv[]) {
  std::string out;
  util::FileStream writing(1);
  uint64_t line_number = 0;
  for (util::StringPiece l : util::FilePiece(0)) {
    preprocess::base64_decode(l, out);
    std::replace(out.begin(), out.end(), '\t', ' ');
    for (util::TokenIter<util::SingleCharacter, true> line(out, '\n'); line; ++line) {
      writing << *line << '\t' << line_number << '\n';
    }
    ++line_number;
  }
}
