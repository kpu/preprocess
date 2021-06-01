#include "util/file_stream.hh"
#include "util/file_piece.hh"
#include "util/murmur_hash.hh"
#include "util/mutable_vocab.hh"
#include "util/tokenize_piece.hh"
#include "util/utf8.hh"
#include "util/utf8_icu.hh"

#include <unordered_map>

#include <boost/lexical_cast.hpp>

namespace {
void SplitLine(util::FilePiece &from, std::vector<util::StringPiece> &to) {
  to.clear();
  for (util::TokenIter<util::SingleCharacter, true> i(from.ReadLine(), ' '); i; ++i) {
    to.push_back(*i);
  }
}

class Recorder {
  public:
    void Add(util::StringPiece source, util::StringPiece target) {
      util::ToLower(target, lowered_);
      uint64_t key = util::MurmurHash64A(lowered_.data(), lowered_.size(), util::MurmurHash64A(source.data(), source.size()));
      ++map_[key][vocab_.FindOrInsert(target)];
    }

    void Dump() {
      util::FileStream out(1);
      for (Map::const_iterator i = map_.begin(); i != map_.end(); ++i) {
        out << boost::lexical_cast<std::string>(i->first);
        for (std::unordered_map<uint32_t, unsigned int>::const_iterator j = i->second.begin(); j != i->second.end(); ++j) {
          out << '\t' << vocab_.String(j->first) << ' ' << j->second;
        }
        out << '\n';
      }
    }

  private:
    util::MutableVocab vocab_;

    std::string lowered_;

    // map_[hash(lowered_target, hash(cased_source))][cased_target] = count(cased_source, cased_target)
    typedef std::unordered_map<uint64_t, std::unordered_map<uint32_t, unsigned int> > Map;
    Map map_;
};

} // namespace

int main(int argc, char *argv[]) {
  if (argc != 4) {
    std::cerr << "Usage: " << argv[0] << " alignment source target\n";
    return 1;
  }
  util::FilePiece align(argv[1], &std::cerr), source_file(argv[2]), target_file(argv[3]);
  std::vector<util::StringPiece> source_words, target_words;
  Recorder recorder;
  std::size_t sentence = 0, discarded = 0;
  for (; ; ++sentence) {
    try {
      SplitLine(source_file, source_words);
    } catch (const util::EndOfFileException &e) { break; }
    SplitLine(target_file, target_words);
    // parse comment lone
    // "# sentence pair (0) source length"
    for (unsigned int i = 0; i < 6; ++i) {
      align.ReadDelimited();
    }
    unsigned long from_length = align.ReadULong();
    align.ReadDelimited(); align.ReadDelimited(); // target length
    unsigned long to_length = align.ReadULong();
    align.ReadLine(); // comment line ending

    align.ReadLine(); // uncased sentence
    util::StringPiece word(align.ReadDelimited());
    UTIL_THROW_IF2("NULL" != word, "Expected NULL at the beginning, not " << word);

    if (from_length != source_words.size() || to_length != target_words.size()) {
      align.ReadLine(); // Complete line.
      ++discarded;
      continue;
    }

    while ("})" != align.ReadDelimited()) {}
    for (unsigned long from = 0; align.ReadWordSameLine(word); ++from) {
      align.ReadWordSameLine(word);
      UTIL_THROW_IF2(word != "({", "Expected ({ not " << word);
      UTIL_THROW_IF2(from >= source_words.size(), "Index " << from << " too high for source text at sentence " << sentence);
      for (align.SkipSpaces(); align.peek() != '}'; align.SkipSpaces()) {
        unsigned long to = align.ReadULong() - 1 /* NULL word */;
        UTIL_THROW_IF2(to >= target_words.size(), "Index " << to << " too high for target text");
        // Throw out beginning of sentence.
        if (from != 0 && to != 0) {
          recorder.Add(source_words[from], target_words[to]);
        }
      }
      UTIL_THROW_IF2(align.ReadDelimited() != "})", "Expected })");
    }
    align.ReadLine(); // Complete line.
  }
  std::cerr << "Discarded " << discarded << "/" << sentence << std::endl;
  recorder.Dump();
}
