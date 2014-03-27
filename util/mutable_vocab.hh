#ifndef UTIL_MUTABLE_VOCAB__
#define UTIL_MUTABLE_VOCAB__

/* A vocabulary mapping class that's mutable at runtime.  The kenlm code has
 * a specialized immutable vocabulary.
 */

#include "util/pool.hh"
#include "util/probing_hash_table.hh"
#include "util/string_piece.hh"

#include <stdint.h>

namespace util {

#pragma pack(push)
#pragma pack(4)
struct MutableVocabInternal {
  typedef uint64_t Key;
  uint64_t GetKey() const { return key; }
  void SetKey(uint64_t to) { key = to; }

  uint64_t key;
  uint32_t id;
};
#pragma pack(pop)
 
class MutableVocab {
  public:
    typedef uint32_t ID;

    static const ID kUNK = 0;

    MutableVocab();
    
    uint32_t Find(const StringPiece &str) const;

    ID FindOrInsert(const StringPiece &str);

    StringPiece String(ID id) const {
      return strings_[id];
    }

    // Includes kUNK.
    std::size_t Size() const { return strings_.size(); }
    
  private:
    util::Pool piece_backing_;

    typedef util::AutoProbing<MutableVocabInternal, util::IdentityHash> Map;
    Map map_;

    std::vector<StringPiece> strings_;
};

} // namespace util
#endif // UTIL_MUTABLE_VOCAB__
