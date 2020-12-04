#include "util/murmur_hash.hh"

#include <iostream>

int main(int argc, char *argv[]) {
  if (argc > 1) {
    std::cerr << "Usage: [stdin] " << argv[0] << std::endl;
    return 1;
  }
  std::string input;
  for (std::string line; std::getline(std::cin, line);) {
    input += line;
  }
  std::cout << std::hex << util::MurmurHashNative(input.c_str(), input.size(), 0) << '\n';
}
