#include "util/fake_ofstream.hh"
#include "util/file_piece.hh"
#include "util/murmur_hash.hh"
#include "util/mutable_vocab.hh"
#include "util/tokenize_piece.hh"
#include "util/utf8.hh"

#define BOOST_LEXICAL_CAST_ASSUME_C_LOCALE
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
} // namespace

int main(int argc, char *argv[]) {
  if (argc != 5) {
    std::cerr << argv[0] << " alignment source target model" << std::endl;
    return 1;
  }
  util::FilePiece align(argv[1]), source_file(argv[2]), target_file(argv[3]), model(argv[4]);

  util::MutableVocab vocab;
  boost::unordered_map<uint64_t, uint32_t> best;
  while (true) {
    uint64_t key;
    try {
      key = model.ReadULong();
    } catch (const util::EndOfFileException &e) { break; }
    uint64_t max_count = 0;
    StringPiece best_word;
    for (util::TokenIter<util::SingleCharacter, true> pair(model.ReadLine(), '\t'); pair; ++pair) {
      util::TokenIter<util::SingleCharacter> spaces(*pair, ' ');
      StringPiece word(*spaces);
      uint64_t count = boost::lexical_cast<uint64_t>(*++spaces);
      if (count > max_count) {
        max_count = count;
        best_word = word;
      }
      best[key] = vocab.FindOrInsert(best_word);
    }
  }

  std::cerr << "Read model." << std::endl;

  std::vector<StringPiece> source_words, target_words;
  std::string lowered;
  util::FakeOFStream out(1);
  for (std::size_t line = 0; ; ++line) {
    try {
      SplitLine(source_file, source_words);
    } catch (const util::EndOfFileException &e) { break; }
    SplitLine(target_file, target_words);
    align.ReadULong();
    UTIL_THROW_IF2("|||" != align.ReadDelimited(), "Expected |||");
    while (SameLine(align)) {
      unsigned long first = align.ReadULong();
      UTIL_THROW_IF2(align.get() != '-', "Bad alignment");
      UTIL_THROW_IF2(align.peek() < '0' || align.peek() > '9', "Expected number for alignment, not " << align.peek());
      unsigned long second = align.ReadULong();
      UTIL_THROW_IF2(first >= source_words.size(), "Index " << first << " too high for source text at line " << line << " which has size " << source_words.size());
      UTIL_THROW_IF2(second >= target_words.size(), "Index " << second << " too high for target text at line " << line << " which has size " << target_words.size());
      utf8::ToLower(target_words[second], lowered);
      StringPiece source(source_words[first]);
      uint64_t key = util::MurmurHash64A(lowered.data(), lowered.size(), util::MurmurHash64A(source.data(), source.size()));
      boost::unordered_map<uint64_t, uint32_t>::const_iterator found = best.find(key);
      if (found != best.end()) {
        target_words[second] = vocab.String(found->second);
      }
    }
    std::vector<StringPiece>::const_iterator i = target_words.begin();
    if (i != target_words.end()) out << *i;
    for (++i; i != target_words.end(); ++i) {
      out << ' ' << *i;
    }
    out << '\n';
  }
}
