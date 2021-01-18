// Tool to convert raw CommonCrawl files into deduplicated files.
// Strips leading and trailing spaces.
// Removes document delimiter lines (those that begin with df6fa1abb58549287111ba8d776733e9).
// Removes duplicate lines.
// Removes any line that contains invalid UTF-8.
//
#include "util/file_stream.hh"
#include "util/file_piece.hh"
#include "util/murmur_hash.hh"
#include "util/probing_hash_table.hh"
#include "util/scoped.hh"
#include "util/utf8.hh"

#include <iostream>

#include <stdint.h>

namespace {

// Hash table with 64-bit keys.
struct Entry {
  typedef uint64_t Key;
  uint64_t key;
  uint64_t GetKey() const { return key; }
  void SetKey(uint64_t to) { key = to; }
};

typedef util::AutoProbing<Entry, util::IdentityHash> Table;

// Use 64-bit MurmurHash in the hash table.  
bool IsNewLine(Table &table, util::StringPiece l) {
  Table::MutableIterator it;
  Entry entry;
  entry.key = util::MurmurHashNative(l.data(), l.size(), 1);
  return !table.FindOrInsert(entry, it);
}

// Remove leading and trailing space characters.
util::StringPiece StripSpaces(util::StringPiece ret) {
  while (ret.size() && util::kSpaces[static_cast<unsigned char>(*ret.data())]) {
    ret = util::StringPiece(ret.data() + 1, ret.size() - 1);
  }
  while (ret.size() && util::kSpaces[static_cast<unsigned char>(ret.data()[ret.size() - 1])]) {
    ret = util::StringPiece(ret.data(), ret.size() - 1);
  }
  return ret;
}


} // namespace

int main(int argc, char *argv[]) {
  if (argc > 2 || (argc == 2 && (!strcmp("-h", argv[1]) || !strcmp("--help", argv[1])))) {
    std::cerr << "Usage: " << argv[0] << " file_to_remove\nLines that appear in file_to_remove will be excluded from the output.\n" << std::endl;
    return 1;
  }
  try {
    Table table;
    util::StringPiece l;

    // If there's a file to remove lines from, add it to the hash table of lines.
    if (argc == 2) {
      util::FilePiece removing(argv[1]);
      while (removing.ReadLineOrEOF(l)) {
        IsNewLine(table, StripSpaces(l));
      }
    }

    // This is the beginning of a line that delimits documents in the raw files.
    const util::StringPiece remove_line("df6fa1abb58549287111ba8d776733e9");
    util::FileStream out(1);
    util::FilePiece in(0, "stdin", &std::cerr);
    while (in.ReadLineOrEOF(l)) {
      l = StripSpaces(l);
      // A line passes if:
      // It does not begin with the magic document delimiter.
      // Its 64-bit hash has not been seen before.
      // and it is valid UTF-8.
      if (!starts_with(l, remove_line) && IsNewLine(table, l) && util::IsUTF8(l)) {
        out << l << '\n';
      }
    }
  } 
  catch (const std::exception &e) {
    std::cerr << e.what() << std::endl;
    return 1;
  }
}
