#include "preprocess/fields.hh"
#include "util/file_stream.hh"
#include "util/file_piece.hh"
#include "util/murmur_hash.hh"
#include "util/pool.hh"
#include "util/probing_hash_table.hh"
#include <vector>

struct Entry {
  typedef uint64_t Key;
  Key key;
  uint64_t GetKey() const { return key; }
  void SetKey(uint64_t to) { key = to; }
  StringPiece value;
};

class RecordCallback {
  public:
    RecordCallback(StringPiece *to) : i_(to) {}

    void operator()(StringPiece str) {
      *(i_++) = str;
    }

    const StringPiece *Position() const { return i_; }

  private:
    StringPiece *i_;
};

int main() {
  std::vector<preprocess::FieldRange> fields;
  fields.resize(4);
  StringPiece segments[4];
  fields[0].begin = 0;
  fields[0].end = 2;
  StringPiece &sentences = segments[1];
  fields[1].begin = 2;
  fields[1].end = 4;
  StringPiece &value = segments[2];
  fields[2].begin = 4;
  fields[2].end = 5;
  StringPiece &after = segments[3];
  fields[3].begin = 5;
  fields[3].end = preprocess::FieldRange::kInfiniteEnd;

  util::Pool string_pool;
  util::FileStream out(1);

  typedef util::AutoProbing<Entry, util::IdentityHash> Table;
  Table table;
  for (StringPiece line : util::FilePiece(0)) {
    RecordCallback cb(segments);
    preprocess::RangeFields(line, fields, '\t', cb);
    UTIL_THROW_IF2(cb.Position() != segments + 4, "Did not get all fields in line " << line);
    Entry entry;
    entry.key = util::MurmurHashNative(sentences.data(), sentences.size());
    Table::MutableIterator it;
    if (table.FindOrInsert(entry, it)) {
       out << StringPiece(line.data(), sentences.data() + sentences.size() - line.data());
       out << '\t' << it->value << '\t';
       out << after;
    } else {
      char *mem = static_cast<char*>(memcpy(string_pool.Allocate(value.size()), value.data(), value.size()));
      it->value = StringPiece(mem, value.size());
      out << line;
    }
    out << '\n';
  }
}
