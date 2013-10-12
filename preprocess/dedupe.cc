#include "util/fake_ofstream.hh"
#include "util/file_piece.hh"
#include "util/murmur_hash.hh"

#include <boost/unordered_set.hpp>

#include <iostream>

int main() {
  try {
    boost::unordered_set<uint64_t> hashes;
    util::FakeOFStream out(1);
    util::FilePiece in(0, "stdin", &std::cerr);
    while (true) {
      StringPiece l = in.ReadLine();
      if (hashes.insert(util::MurmurHashNative(l.data(), l.size())).second) {
        out << l << '\n';
      }
    }
  } 
  catch (const util::EndOfFileException &e) { return 0; }
  catch (const std::exception &e) {
    std::cerr << e.what() << std::endl;
    return 1;
  }
}
