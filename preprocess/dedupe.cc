#include "util/file_piece.hh"
#include "util/murmur_hash.hh"

#include <boost/unordered_set.hpp>

#include <iostream>

#include <inttypes.h>
#include <err.h>

int main() {
  boost::unordered_set<uint64_t> hashes;
  try {
    util::FilePiece f(0, "stdin", &std::cerr);
    while (true) {
      StringPiece l = f.ReadLine();
      if (hashes.insert(util::MurmurHashNative(l.data(), l.size())).second) {
        if ((fwrite(l.data(), l.size(), 1, stdout) != 1 && !l.empty()) || ('\n' != putc('\n', stdout))) err(1, "write failed.");
      }
    }
  } catch (const util::EndOfFileException &e) {}
}
