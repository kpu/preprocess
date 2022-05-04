#include "util/file_piece.hh"
#include "util/file_stream.hh"
#include "util/murmur_hash.hh"
#include "util/probing_hash_table.hh"

#include <iostream>

struct Entry {
  typedef uint64_t Key;
  uint64_t key;
  uint64_t GetKey() const { return key; }
  void SetKey(uint64_t to) { key = to; }
};

int main(int argc, char *argv[]) {
  if (argc != 2) {
    std::cerr << "Usage: " << argv[0] << " subtract <from >output\n"
      "Copies from stdin to stdout, skipping lines that appear in `subtract`.\n"
      "The subtraction is approximate, based on the hash of the line.\n"
      "This is set subtraction.  All copies of a line are removed.\n";
    return 1;
  }
  util::AutoProbing<Entry, util::IdentityHash> table;
  // Load subtraction into table.
  for (util::StringPiece line : util::FilePiece(argv[1])) {
    Entry entry;
    entry.key = util::MurmurHashNative(line.data(), line.size(), 1);
    util::AutoProbing<Entry, util::IdentityHash>::MutableIterator it;
    table.FindOrInsert(entry, it);
  }
  util::FileStream out(1);
  for (util::StringPiece line : util::FilePiece(0)) {
    uint64_t key = util::MurmurHashNative(line.data(), line.size(), 1);
    util::AutoProbing<Entry, util::IdentityHash>::ConstIterator it;
    if (!table.Find(key, it)) {
      out << line << '\n';
    }
  }
}
