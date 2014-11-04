#include "util/fake_ofstream.hh"
#include "util/file_piece.hh"
#include "util/utf8.hh"

int main() {
  util::FilePiece in(0);
  util::FakeOFStream out(1);
  StringPiece line;
  while (in.ReadLineOrEOF(line)) {
    if (utf8::IsUTF8(line)) {
      out << line << '\n';
    }
  }
}
