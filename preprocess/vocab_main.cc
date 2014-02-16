#include "util/file_piece.hh"
#include "util/fake_ofstream.hh"
#include "util/murmur_hash.hh"
#include "util/probing_hash_table.hh"

#include <boost/unordered_set.hpp>

#include <iostream>

#include <string.h>

struct Entry {
  typedef uint64_t Key;
  uint64_t key;
  uint64_t GetKey() const { return key; }
  void SetKey(uint64_t to) { key = to; }
};


int main() {
  bool delimiters[256];
  memset(delimiters, 0, sizeof(delimiters));
  delimiters['\0'] = true;
  delimiters['\t'] = true;
  delimiters['\r'] = true;
  delimiters['\n'] = true;
  delimiters[' '] = true;

  util::AutoProbing<Entry, util::IdentityHash> seen;

  util::FilePiece in(0, "stdin", &std::cerr);
  util::FakeOFStream out(1);

  util::AutoProbing<Entry, util::IdentityHash>::MutableIterator it;
  Entry entry;

  try { while (true) {
    StringPiece word = in.ReadDelimited(delimiters);
    entry.SetKey(util::MurmurHashNative(word.data(), word.size()));
    if (!seen.FindOrInsert(entry, it)) {
      out << word << '\0';
    }
  } } catch (const util::EndOfFileException &e) {}
}
