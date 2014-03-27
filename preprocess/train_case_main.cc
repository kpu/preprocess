#include "util/fake_ofstream.hh"
#include "util/file_piece.hh"
#include "util/murmur_hash.hh"
#include "util/mutable_vocab.hh"
#include "util/tokenize_piece.hh"
#include "util/utf8.hh"

#include <boost/lexical_cast.hpp>
#include <boost/unordered_map.hpp>

namespace {
void SplitLine(util::FilePiece &from, std::vector<StringPiece> &to) {
  to.clear();
  for (util::TokenIter<util::SingleCharacter, true> i(from.ReadLine(), ' '); i; ++i) {
    to.push_back(*i);
  }
}

bool SameLine(util::FilePiece &f) {
  while (true) {
    switch(f.peek()) {
      case '\n':
        f.get();
        return false;
      case ' ':
      case '\t':
        f.get();
        continue;
      default:
        return true;
    }
  }
}

class Recorder {
  public:
    void Add(StringPiece source, StringPiece target) {
      utf8::ToLower(target, lowered_);
      uint64_t key = util::MurmurHash64A(lowered_.data(), lowered_.size(), util::MurmurHash64A(source.data(), source.size()));
      ++map_[key][vocab_.FindOrInsert(target)];
    }

    void Dump() {
      util::FakeOFStream out(1);
      for (Map::const_iterator i = map_.begin(); i != map_.end(); ++i) {
        out << boost::lexical_cast<std::string>(i->first);
        for (boost::unordered_map<uint32_t, unsigned int>::const_iterator j = i->second.begin(); j != i->second.end(); ++j) {
          out << '\t' << vocab_.String(j->first) << ' ' << j->second;
        }
        out << '\n';
      }
    }

  private:
    util::MutableVocab vocab_;

    std::string lowered_;

    // map_[hash(lowered_target, hash(cased_source))][cased_target] = count(cased_source, cased_target)
    typedef boost::unordered_map<uint64_t, boost::unordered_map<uint32_t, unsigned int> > Map;
    Map map_;
};

} // namespace

int main(int argc, char *argv[]) {
  if (argc != 4) {
    std::cerr << "Usage: " << argv[0] << " alignment source target\n";
    return 1;
  }
  util::FilePiece align(argv[1], &std::cerr), source_file(argv[2]), target_file(argv[3]);
  std::vector<StringPiece> source_words, target_words;
  Recorder recorder;
  while (true) {
    try {
      SplitLine(source_file, source_words);
    } catch (const util::EndOfFileException &e) { break; }
    SplitLine(target_file, target_words);
    while (SameLine(align)) {
      unsigned long first = align.ReadULong();
      UTIL_THROW_IF2(align.get() != '-', "Bad alignment");
      UTIL_THROW_IF2(align.peek() < '0' || align.peek() > '9', "Expected number for alignment, not " << align.peek());
      unsigned long second = align.ReadULong();
      UTIL_THROW_IF2(first >= source_words.size(), "Index " << first << " too high for source text");
      UTIL_THROW_IF2(second >= target_words.size(), "Index " << second << " too high for target text");
      // Throw out beginning of sentence.
      if (first != 0 && second != 0) {
        recorder.Add(source_words[first], target_words[second]);
      }
    }
  }
  recorder.Dump();
}
