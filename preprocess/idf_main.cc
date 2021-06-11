/* Computes inverse document frequency for each token seen in the input.  A document is a line. */
#include "util/file_piece.hh"
#include "util/murmur_hash.hh"
#include "util/pool.hh"
#include "util/probing_hash_table.hh"
#include "util/tokenize_piece.hh"
#include "util/file_stream.hh"

#include <cmath>
#include <unordered_set>

struct Entry {
  typedef uint64_t Key;
  uint64_t hash;

  uint64_t GetKey() const { return hash; }
  void SetKey(uint64_t to) { hash = to; }

  // Should be allocated from pool to ensure survival.
  util::StringPiece str;

  uint64_t document_count;
};

int main() {
  uint64_t documents = 0;
  util::Pool strings;
  util::AutoProbing<Entry, util::IdentityHash> words;
  Entry ent;
  ent.document_count = 1;
  for (util::StringPiece line : util::FilePiece(0)) {
    ++documents;
    std::unordered_set<uint64_t> seen_in_line;
    for (util::TokenIter<util::BoolCharacter, true> it(line, util::kSpaces); it; ++it) {
      ent.hash = util::MurmurHashNative(it->data(), it->size());
      if (seen_in_line.insert(ent.hash).second) {
        // Newly seen in this line.
        util::AutoProbing<Entry, util::IdentityHash>::MutableIterator words_it;
        if (words.FindOrInsert(ent, words_it)) {
          ++(words_it->document_count);
        } else {
          char *data = static_cast<char*>(strings.Allocate(it->size()));
          memcpy(data, it->data(), it->size());
          words_it->str = util::StringPiece(data, it->size());
        }
      }
    }
  }
  double documents_log = std::log(static_cast<double>(documents));
  util::FileStream out(1);
  for (util::AutoProbing<Entry, util::IdentityHash>::ConstIterator i = words.RawBegin(); i != words.RawEnd(); ++i) {
    if (i->GetKey()) {
      double count = static_cast<double>(i->document_count);
      double idf = documents_log - std::log(count);
      out << i->str << ' ' << idf << '\n';
    }
  }
}
