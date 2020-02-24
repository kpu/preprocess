#include "preprocess/fields.hh"
#include "util/fake_ofstream.hh"
#include "util/file_piece.hh"
#include "util/fixed_array.hh"
#include "util/murmur_hash.hh"

#include <sstream>
#include <iomanip>

#include <boost/program_options.hpp>
#include <boost/program_options/positional_options.hpp>

namespace preprocess {

struct Options {
  std::vector<FieldRange> key_fields;
  char delim;
  std::vector<std::string> outputs;
};

void ParseArgs(int argc, char *argv[], Options &out) {
  namespace po = boost::program_options;
  po::options_description desc("Arguments");
  std::string fields;
  std::string prefix;
  unsigned int number;

  desc.add_options()
    ("help,h", po::bool_switch(), "Show this help message")
    ("fields,f", po::value(&fields)->default_value("1-"), "Fields to use for key like cut -f")
    ("delim,d", po::value(&out.delim)->default_value('\t'), "Field delimiter")
    ("prefix,p", po::value(&prefix), "Prefix and count of outputs")
    ("number,n", po::value(&number), "Number of shards")
    ("output,o", po::value(&out.outputs)->multitoken(), "Output file names (or just list them without -o)");

  po::positional_options_description pd;
  pd.add("output", -1);

  po::variables_map vm;
  po::store(po::command_line_parser(argc, argv).options(desc).positional(pd).run(), vm);
  if (argc == 1 || vm["help"].as<bool>()) {
    std::cerr << 
      "Shards stdin into multiple files by the hash of the key.\n" <<
      "Output is specified as --prefix prefix --number n or just listing file names.\n" <<
       desc <<
      "Examples:\n" <<
      argv[0] << " a b             #Shards stdin to files a and b using the whole line as key.\n" <<
      argv[0] << " a b c           #Shards stdin to files a, b, and c using the whole line as key.\n" <<
      argv[0] << " -f 1 a b        #Shards stdin to files a and b using tab-delimited field 1.\n" <<
      argv[0] << " -d ' ' -f 1 a b #Shards stdin to files a and b using space-delimited field 1." << std::endl;
    exit(1);
  }
  po::notify(vm);

  ParseFields(fields.c_str(), out.key_fields);
  DefragmentFields(out.key_fields);

  if (out.outputs.empty()) {
    UTIL_THROW_IF2(!vm.count("prefix"), "Specify outputs using --outputs or e.g. --prefix pre --number 2");
    UTIL_THROW_IF2(!vm.count("number"), "--prefix specified but we need to know how many shards with -n");
    // How many digits will be in the 0-indexed representation?
    unsigned int digits = 0;
    for (unsigned int compare = number - 1; compare; ++digits, compare /= 10) {}
    std::ostringstream stream;
    stream << std::setfill('0') << std::setw(digits);
    for (unsigned int i = 0; i < number; ++i) {
      stream << std::setw(digits) << i;
      out.outputs.push_back(prefix + stream.str());
      stream.str(std::string());
      stream.clear();
    }
  } else {
    UTIL_THROW_IF2(vm.count("prefix"), "Specify --prefix or --output");
    UTIL_THROW_IF2(vm.count("number") && number != out.outputs.size(), "Number of outputs does not match");
  }
}

} // namespace preprocess

int main(int argc, char *argv[]) {
  preprocess::Options options;
  preprocess::ParseArgs(argc, argv, options);
  uint64_t shard_count = options.outputs.size();

  util::FilePiece in(0);
  StringPiece line;
  util::FixedArray<util::FakeOFStream> out(options.outputs.size());
  std::string output(argv[1]);
  for (const std::string &o : options.outputs) {
    out.push_back(util::CreateOrThrow(o.c_str()));
  }
  while (in.ReadLineOrEOF(line)) {
    preprocess::HashCallback cb;
    preprocess::RangeFields(line, options.key_fields, options.delim, cb);
    out[cb.Hash() % shard_count] << line << '\n';
  }
  return 0;
}
