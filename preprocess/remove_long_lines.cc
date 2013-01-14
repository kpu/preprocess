#include "util/file_piece.hh"

#include <iostream>

#include <err.h>

int main() {
  try {
    util::FilePiece f(0, NULL, &std::cerr);
    while (true) {
      StringPiece l = f.ReadLine();
      if (l.size() > 2000) continue;
      if ((fwrite(l.data(), l.size(), 1, stdout) != 1 && !l.empty()) || ('\n' != putc('\n', stdout))) err(1, "write failed.");
    }
  } catch (const util::EndOfFileException &e) {}
}
