#include "util/murmur_hash.hh"

#include <iostream>
#include <cstring>
#include <memory>
#include <vector>

int main(int argc, char *argv[]) {
  if (argc > 1) {
    std::cerr << "Usage: [stdin] " << argv[0] << std::endl;
    return 1;
  }
  
  constexpr size_t bufferSize = 1024*1024;
  std::vector<char> buffer(bufferSize);
  uint64_t chained_hash = 0;
  
  while (std::cin)
  {
    std::cin.read(&buffer[0], bufferSize);
    if(std::cin.bad()){
	    std::cerr << "Error trying to read from stdin\n";
	    return 1;
    }
    size_t count = std::cin.gcount();
    if (!count)
      break;
    chained_hash = util::MurmurHashNative(&buffer[0], count, chained_hash);
  }
  std::cout << std::hex << chained_hash << '\n';
}
