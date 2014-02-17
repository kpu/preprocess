#include "util/file_piece.hh"
#include "util/fake_ofstream.hh"
#include "util/murmur_hash.hh"
#include "util/pool.hh"
#include "util/probing_hash_table.hh"
#include "util/tokenize_piece.hh"
#include "util/utf8.hh"

uint64_t Hash(const StringPiece &str) {
  return util::MurmurHashNative(str.data(), str.size());
}

class Truecase {
  public:
    explicit Truecase(const char *file);

    // Apply truecasing, using temp as a buffer (to remain const and fast).
    void Apply(const StringPiece &line, std::string &temp, util::FakeOFStream &out) const;

  private:
    struct TableEntry {
      typedef uint64_t Key;
      Key key;
      uint64_t GetKey() const { return key; }
      void SetKey(uint64_t to) { key = to; }
      
      const char *best;
      bool sentence_end;
      bool delayed_sentence_start;
    };

    TableEntry &Insert(StringPiece word) {
      TableEntry entry;
      entry.key = Hash(word);
      entry.sentence_end = false;
      entry.delayed_sentence_start = false;
      Table::MutableIterator it;
      if (!table_.FindOrInsert(entry, it)) {
        char *start = static_cast<char*>(memcpy(string_pool_.Allocate(word.size() + 1), word.data(), word.size()));
        start[word.size()] = '\0';
        it->best = start;
      }
      return *it;
    }

    void InsertKnown(StringPiece word, const char *best) {
      TableEntry entry;
      entry.key = Hash(word);
      entry.sentence_end = false;
      entry.delayed_sentence_start = false;
      entry.best = best;
      Table::MutableIterator it;
      table_.FindOrInsert(entry, it);
    }

    util::Pool string_pool_;

    typedef util::AutoProbing<TableEntry, util::IdentityHash> Table;

    Table table_;
};

Truecase::Truecase(const char *file) {
  // Sentence ends.
  const char *kEndSentence[] = { ".", ":", "?", "!"};
  for (const char *const *i = kEndSentence; i != kEndSentence + sizeof(kEndSentence) / sizeof(const char*); ++i)
    Insert(*i).sentence_end = true;

  // Delays sentence start.
  const char *kDelayedSentenceStart[] = {"(", "[", "\"", "'", "&apos;", "&quot;", "&#91;", "&#93;"};
  for (const char *const *i = kDelayedSentenceStart; i != kDelayedSentenceStart + sizeof(kDelayedSentenceStart) / sizeof(const char*); ++i)
    Insert(*i).delayed_sentence_start = true;

  StringPiece word;
  std::string lower;
  for (util::FilePiece f(file); f.ReadWordSameLine(word); f.ReadLine()) {
    const TableEntry &top = Insert(word);
    utf8::ToLower(word, lower);
    if (word != lower) {
      InsertKnown(lower, top.best);
    }
    // Discard every other token (which is statistics)
    while (f.ReadWordSameLine(word) && f.ReadWordSameLine(word)) {
      // These secondary casings reference the same best casing.
      InsertKnown(word, top.best);
    }
  }
}

void Truecase::Apply(const StringPiece &line, std::string &temp, util::FakeOFStream &out) const {
  bool sentence_start = true;
  for (util::TokenIter<util::BoolCharacter, true> word(line, util::kSpaces); word;) {
    const TableEntry *entry;
    bool entry_found = table_.Find(Hash(*word), entry);
    // If they're known and not the beginning of sentence, pass through.
    if (entry_found && !sentence_start) {
      out << *word;
    } else {
      utf8::ToLower(*word, temp);
      const TableEntry *lower;
      if (table_.Find(Hash(temp), lower)) {
        // If there's a best form, print it.
        out << lower->best;
      } else {
        // Pass unknowns through.
        out << *word;
      }
    }
    if (entry_found) {
      if (entry->sentence_end) {
        sentence_start = true;
      } else if (!entry->delayed_sentence_start) {
        sentence_start = false;
      }
    }
    if (++word) out << ' ';
  }
  out << '\n';
}

int main(int argc, char *argv[]) {
  if (argc != 3 || strcmp(argv[1], "--model")) {
    std::cerr << "truecase --model $model <in >out" << std::endl;
    return 1;
  }
  Truecase caser(argv[2]);
  util::FakeOFStream out(1);
  StringPiece line;
  std::string temp;
  for (util::FilePiece f(0); f.ReadLineOrEOF(line);) {
    caser.Apply(line, temp, out);
  }
  return 0;
}
