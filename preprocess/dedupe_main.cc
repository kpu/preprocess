#include "fields.hh"
#include "parallel.hh"
#include "util/murmur_hash.hh"
#include "util/probing_hash_table.hh"
#include "util/scoped.hh"

#include <boost/program_options.hpp>
#include <boost/program_options/positional_options.hpp>

#include <iostream>

#include <stdint.h>

namespace preprocess {
namespace {

struct Options {
  std::vector<FieldRange> key_fields;
  char delim;
  std::vector<std::string> files;
};

void ParseArgs(int argc, char *argv[], Options &out) {
  namespace po = boost::program_options;
  po::options_description desc("Deduplication settings");
  std::string fields;

  desc.add_options()
    ("help,h", po::bool_switch(), "Show this help message")
    ("fields,f", po::value(&fields)->default_value("1-"), "Fields to use for key like cut -f")
    ("delim,d", po::value(&out.delim)->default_value('\t'), "Field delimiter")
    ("parallel,p", po::value(&out.files)->multitoken(), "Filter parallel data using four files: in_en in_fr out_en out_fr");
  po::positional_options_description pd;
  pd.add("parallel", -1);

  po::variables_map vm;
  po::store(po::command_line_parser(argc, argv).options(desc).positional(pd).run(), vm);
  if (vm["help"].as<bool>() || (!out.files.empty() && out.files.size() != 4)) {
    std::cerr <<
      "Deduplicate lines in a file.\n"
      "Only 64-bit hashes are kept.  In the event of a hash collision, a unique line\n"
      "will be removed.\n"
      "By default the entire line is used as the key for equality.  Using -f and -d\n"
      "similar to cut, the key can be restricted to some columns.  The line containing\n"
      "the first instance of the key is preserved, while the rest are removed.\n" <<
      desc <<
      "Deduplicate lines in a file: " << argv[0] << " <in >out\n"
      "Deduplicate parallel data, removing if either side is non-unique " << argv[0] << " -p in_en in_fr out_en out_fr\n";
    exit(1);
  }
  po::notify(vm);

  ParseFields(fields.c_str(), out.key_fields);
  DefragmentFields(out.key_fields);
}

struct Entry {
  typedef uint64_t Key;
  uint64_t key;
  uint64_t GetKey() const { return key; }
  void SetKey(uint64_t to) { key = to; }
};

class Dedupe {
  public:
    bool operator()(const util::StringPiece &line) {
      return (*this)(util::MurmurHashNative(line.data(), line.size(), 1));
    }

    bool operator()(uint64_t key) {
      Entry entry;
      entry.key = key;
      Table::MutableIterator it;
      return !table_.FindOrInsert(entry, it);
    }

  private:
    typedef util::AutoProbing<Entry, util::IdentityHash> Table;
    Table table_;
};

class FieldDedupe : public Dedupe {
  public:
    explicit FieldDedupe(const Options &options)
      : key_fields_(options.key_fields), delim_(options.delim) {}

    bool operator()(const util::StringPiece &line) {
      HashCallback hasher(1);
      RangeFields(line, key_fields_, delim_, hasher);
      return (*static_cast<Dedupe*>(this))(hasher.Hash());
    }

  private:
    std::vector<FieldRange> key_fields_;
    char delim_;
};

} // namespace
} // namespace preprocess

int main(int argc, char *argv[]) {
  preprocess::Options options;
  ParseArgs(argc, argv, options);

  if (options.key_fields.size() == 1 && options.key_fields[0].begin == 0 && options.key_fields[0].end == preprocess::FieldRange::kInfiniteEnd) {
    return preprocess::FilterParallel<preprocess::Dedupe>(options.files);
  } else {
    return preprocess::FilterParallel<preprocess::FieldDedupe>(options.files, options);
  }
}
