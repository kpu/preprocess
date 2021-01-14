#include "util/murmur_hash.hh"

#include <iostream>
#include <cstring>

int main(int argc, char *argv[]) {
  if (argc > 1) {
    std::cerr << "Usage: [stdin] " << argv[0] << std::endl;
    return 1;
  }
  
  constexpr size_t bufferSize = 1024*1024;
  char* buffer = new char[bufferSize];
  uint64_t chained_hash = 0;
  
  while (std::cin)
  {
    std::cin.read(buffer, bufferSize);
    size_t count = std::cin.gcount();
    if (!count)
      break;
    chained_hash = util::MurmurHashNative(buffer, count, chained_hash);
  }
  std::cout << std::hex << chained_hash << '\n';
  delete[] buffer;
}
