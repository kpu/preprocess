#include "util/file_stream.hh"
#include "util/file_piece.hh"
#include "util/utf8.hh"

int main() {
  util::FilePiece in(0);
  util::FileStream out(1);
  util::StringPiece line;
  while (in.ReadLineOrEOF(line)) {
    if (util::IsUTF8(line)) {
      out << line << '\n';
    }
  }
}
