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
  std::size_t estimate = (argc == 2) ? boost::lexical_cast<std::size_t>(argv[1]) : 100000000;
  typedef util::ProbingHashTable<Entry, util::IdentityHash> Table;
  try {
    util::scoped_malloc table_backing(util::CallocOrThrow(Table::Size(estimate, 1.5)));
    Table table(table_backing.get(), Table::Size(estimate, 1.5));
    std::size_t double_cutoff = estimate * 1.2;
    util::FakeOFStream out(1);
    util::FilePiece in(0, "stdin", NULL);
    while (true) {
      StringPiece l = in.ReadLine();
      Entry entry;
      Table::MutableIterator it;
      entry.key = util::MurmurHashNative(l.data(), l.size()) + 1;
      if (!table.FindOrInsert(entry, it)) {
        out << l << '\n';
        if (table.SizeNoSerialization() > double_cutoff) {
          table_backing.call_realloc(table.DoubleTo());
          table.Double(table_backing.get());
          double_cutoff *= 2;
        }
      }
    }
  } 
  catch (const util::EndOfFileException &e) { return 0; }
  catch (const std::exception &e) {
    std::cerr << e.what() << std::endl;
    return 1;
  }
}
