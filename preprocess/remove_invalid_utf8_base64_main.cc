#include "util/file_stream.hh"
#include "util/file_piece.hh"
#include "util/utf8.hh"

#include "base64.hh"

int main() {
  util::FilePiece in(0);
  util::FileStream out(1);
  util::StringPiece line;
  std::string decoded;
  std::string empty_base64;
  preprocess::base64_encode("", empty_base64);
  while (in.ReadLineOrEOF(line)) {
    preprocess::base64_decode(line, decoded);
    if (util::IsUTF8(decoded)) {
      out << line << '\n';
    } else {
      out << empty_base64 << '\n';   
    }
  }
}
