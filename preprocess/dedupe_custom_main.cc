#include "util/fake_ofstream.hh"
#include "util/file_piece.hh"
#include "util/murmur_hash.hh"
#include "util/probing_hash_table.hh"
#include "util/scoped.hh"

#include <boost/lexical_cast.hpp>

#include <iostream>

#include <stdint.h>

namespace {

struct Entry {
  typedef uint64_t Key;
  uint64_t key;
  uint64_t GetKey() const { return key; }
  void SetKey(uint64_t to) { key = to; }
};

typedef util::AutoProbing<Entry, util::IdentityHash> Table;

bool IsNewLine(Table &table, StringPiece l) {
  Table::MutableIterator it;
  Entry entry;
  entry.key = util::MurmurHashNative(l.data(), l.size(), 1);
  return !table.FindOrInsert(entry, it);
}

StringPiece StripSpaces(StringPiece ret) {
  while (ret.size() && util::kSpaces[static_cast<unsigned char>(*ret.data())]) {
    ret = StringPiece(ret.data() + 1, ret.size() - 1);
  }
  while (ret.size() && util::kSpaces[static_cast<unsigned char>(ret.data()[ret.size() - 1])]) {
    ret = StringPiece(ret.data(), ret.size() - 1);
  }
  return ret;
}


} // namespace

int main(int argc, char *argv[]) {
  if (argc != 2) {
    std::cerr << "Usage: " << argv[0] << " file_to_remove\nLines in file_to_remove will be removed from the input.\n" << std::endl;
    return 1;
  }
  try {
    Table table;
    StringPiece l;

    {
      util::FilePiece removing(argv[1]);
      while (removing.ReadLineOrEOF(l)) {
        IsNewLine(table, StripSpaces(l));
      }
    }

    StringPiece remove_line("df6fa1abb58549287111ba8d776733e9");
    util::FakeOFStream out(1);
    util::FilePiece in(0, "stdin", &std::cerr);
    while (in.ReadLineOrEOF(l)) {
      l = StripSpaces(l);
      // Remove lines beginning with Christian's magic token.
      if (starts_with(l, remove_line)) continue;
      if (IsNewLine(table, l)) {
        out << l << '\n';
      }
    }
  } 
  catch (const std::exception &e) {
    std::cerr << e.what() << std::endl;
    return 1;
  }
}
