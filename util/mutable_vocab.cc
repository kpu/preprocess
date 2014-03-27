#include "util/mutable_vocab.hh"

#include "util/murmur_hash.hh"

namespace util {

MutableVocab::MutableVocab() {
  strings_.push_back(StringPiece("<unk>"));
}

MutableVocab::ID MutableVocab::Find(const StringPiece &str) const {
  Map::ConstIterator it;
  if (map_.Find(util::MurmurHashNative(str.data(), str.size()), it)) {
    return it->id;
  } else {
    return kUNK;
  }
}

uint32_t MutableVocab::FindOrInsert(const StringPiece &str) {
  MutableVocabInternal entry;
  entry.key = util::MurmurHashNative(str.data(), str.size());
  Map::MutableIterator it;
  if (map_.FindOrInsert(entry, it)) {
    return it->id;
  }
  it->id = strings_.size();
  
  char *copied = static_cast<char*>(piece_backing_.Allocate(str.size()));
  memcpy(copied, str.data(), str.size());
  strings_.push_back(StringPiece(copied, str.size()));
  return it->id;
}

} // namespace util
