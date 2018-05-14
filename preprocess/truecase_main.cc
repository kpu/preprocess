#include "util/file_piece.hh"
#include "util/fake_ofstream.hh"
#include "util/murmur_hash.hh"
#include "util/pool.hh"
#include "util/probing_hash_table.hh"
#include "util/tokenize_piece.hh"
#include "util/utf8.hh"

#include <string.h>

#include <iostream>

uint64_t Hash(const StringPiece &str) {
  return util::MurmurHash64A(str.data(), str.size());
}

const std::size_t kStringSize = 6374807;
const std::size_t kHashEntries = 1085145;
const std::size_t kHashSize = 19098544;
const std::size_t kModelSize = kStringSize + kHashSize;

class ModelFile {
  public:
    explicit ModelFile(const char *name) : file_(util::OpenReadOrThrow(name)) {
      MapRead(util::POPULATE_OR_READ, file_.get(), 0, kModelSize, mem_);
    }

    const char *Base() const {
      return mem_.begin();
    }
    char *Base() {
      return mem_.begin();
    }

    void *HashTable() {
      return mem_.begin() + kStringSize;
    }

  private:
    util::scoped_fd file_;
    util::scoped_memory mem_;

    char *at_;
};

class Truecase {
  public:
    explicit Truecase(const char *file);

    ~Truecase() {}

    // Apply truecasing, using temp as a buffer (to remain const and fast).
    void Apply(const StringPiece &line, std::string &temp, util::FakeOFStream &out) const;

  private:
    struct TableEntry {
      typedef uint64_t Key;
      Key key;
      uint64_t GetKey() const { return key; }
      void SetKey(uint64_t to) { key = to; }
      
      // Offset of best from base string pointer.
      uint32_t best;
      // If only the uppercase version is known, the lowercase version will still be in the hash table.
      bool known;
      bool sentence_end;
      bool delayed_sentence_start;
    };

    ModelFile model_;

    typedef util::ProbingHashTable<TableEntry, util::IdentityHash> Table;

    Table table_;
};

Truecase::Truecase(const char *file) : model_(file), table_(model_.HashTable(), kHashSize) {}

void Truecase::Apply(const StringPiece &line, std::string &temp, util::FakeOFStream &out) const {
  bool sentence_start = true;
  for (util::TokenIter<util::BoolCharacter, true> word(line, util::kSpaces); word;) {
    const TableEntry *entry;
    bool entry_found = table_.Find(Hash(*word), entry);
    // If they're known and not the beginning of sentence, pass through.
    if (entry_found && entry->known && !sentence_start) {
      out << *word;
    } else {
      try {
        utf8::ToLower(*word, temp);
      } catch (const utf8::NotUTF8Exception &e) {
        std::cerr << e.what() << "\nSkipping this word.\n";
        continue;
      }
      const TableEntry *lower;
      if (table_.Find(Hash(temp), lower)) {
        // If there's a best form, print it.
        out << model_.Base() + lower->best;
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
    } else {
      sentence_start = false;
    }
    if (++word) out << ' ';
  }
  out << '\n';
}

int main(int argc, char *argv[]) {
  if (argc != 3 || (strcmp(argv[1], "--model") && strcmp(argv[1], "-model"))) {
    std::cerr << "Fast reimplementation of Moses scripts/recaser/truecase.perl except it does not support factors." << std::endl;
    std::cerr << argv[0] << " --model $model <in >out" << std::endl;
    return 1;
  }
  utf8::Init();
  Truecase caser(argv[2]);
  util::FakeOFStream out(1);
  StringPiece line;
  std::string temp;
  for (util::FilePiece f(0); f.ReadLineOrEOF(line);) {
    caser.Apply(line, temp, out);
  }
  return 0;
}
