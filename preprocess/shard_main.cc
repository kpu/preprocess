#include "util/fake_ofstream.hh"
#include "util/file_piece.hh"
#include "util/fixed_array.hh"
#include "util/murmur_hash.hh"

#include <boost/lexical_cast.hpp>

int main(int argc, char *argv[]) {
  if (argc != 3) {
    std::cerr << "Usage: " << argv[0] << " file_prefix shard_count\n"
      "Shards stdin into multiple files by the hash of the line.\n"
      "The files will be named as file_prefix0 file_prefix1 etc.\n";
    return 1;
  }
  util::FilePiece in(0);
  StringPiece line;
  uint64_t shard_count = boost::lexical_cast<unsigned>(argv[2]);
  util::FixedArray<util::FakeOFStream> out(shard_count);
  std::string output(argv[1]);
  for (uint64_t i = 0; i < shard_count; ++i) {
    out.push_back(util::CreateOrThrow((output + boost::lexical_cast<std::string>(i)).c_str()));
  }
  while (in.ReadLineOrEOF(line)) {
    out[util::MurmurHashNative(line.data(), line.size(), 47849374332489ULL /* Be different from deduper */) % shard_count] << line << '\n';
  }
  return 0;
}
