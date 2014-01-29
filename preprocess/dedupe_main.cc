#include "util/fake_ofstream.hh"
#include "util/file_piece.hh"
#include "util/murmur_hash.hh"
#include "util/probing_hash_table.hh"
#include "util/scoped.hh"

#include <boost/lexical_cast.hpp>

#include <iostream>

#include <stdint.h>

struct Entry {
  typedef uint64_t Key;
  uint64_t key;
  uint64_t GetKey() const { return key; }
  void SetKey(uint64_t to) { key = to; }
};

int main(int argc, char *argv[]) {
  try {
    std::size_t estimate = (argc == 2) ? boost::lexical_cast<std::size_t>(argv[1]) : 100000000;
    typedef util::AutoProbing<Entry, util::IdentityHash> Table;
    Table table(estimate);
    uint64_t lines = 0;
    util::FilePiece in(0, "stdin", NULL);
    util::FakeOFStream out(1);
    StringPiece l;
    while (true) {
      try {
        l = in.ReadLine();
      } catch (const util::EndOfFileException &e) { break; }
      ++lines;
      Entry entry;
      Table::MutableIterator it;
      entry.key = util::MurmurHashNative(l.data(), l.size()) + 1;
      if (!table.FindOrInsert(entry, it)) {
        out << l << '\n';
      }
    }
    std::cerr << "Kept " << table.Size() << " / " << lines << " = " << (static_cast<float>(table.Size()) / static_cast<float>(lines)) << std::endl;
  } catch (const std::exception &e) {
    std::cerr << e.what() << std::endl;
    return 1;
  }
  return 0;
}
