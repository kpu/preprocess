#include "util/file_piece.hh"
#include "util/fake_ofstream.hh"
#include "util/murmur_hash.hh"

#include <boost/unordered_set.hpp>

#include <iostream>

#include <string.h>

int main() {
  bool delimiters[256];
  memset(delimiters, 0, sizeof(delimiters));
  delimiters['\0'] = true;
  delimiters['\t'] = true;
  delimiters['\r'] = true;
  delimiters['\n'] = true;
  delimiters[' '] = true;

  boost::unordered_set<uint64_t> seen;

  util::FilePiece in(0, "stdin", &std::cerr);
  util::FakeOFStream out(1);

  try { while (true) {
    StringPiece word = in.ReadDelimited(delimiters);
    if (seen.insert(util::MurmurHashNative(word.data(), word.size())).second) {
      out << word << '\0';
    }
  } } catch (const util::EndOfFileException &e) {}
}
