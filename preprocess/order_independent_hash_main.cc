#include "util/murmur_hash.hh"
#include "util/file_piece.hh"

int main() {
  uint64_t sum = 0;
  for (util::StringPiece line : util::FilePiece(0)) {
    sum += util::MurmurHash64A(line.data(), line.size());
  }
  std::cout << sum << std::endl;
}
