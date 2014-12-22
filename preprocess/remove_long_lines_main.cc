#include "util/fake_ofstream.hh"
#include "util/file_piece.hh"

#include <boost/lexical_cast.hpp>
#include <iostream>

#include <err.h>

int main(int argc, char *argv[]) {
  std::size_t limit;
  if (argc == 1) {
    limit = 2000;
  } else if (argc == 2) {
    limit = boost::lexical_cast<std::size_t>(argv[2]);
  } else {
    std::cerr << "Usage: " << argv[0] << " [length limit in bytes]" << std::endl;
    return 1;
  }
  util::FilePiece f(0, NULL, &std::cerr);
  util::FakeOFStream out(1);
  try {
    while (true) {
      StringPiece l = f.ReadLine();
      if (l.size() <= limit) {
        out << l << '\n';
      }
    }
  } catch (const util::EndOfFileException &e) {}
}
