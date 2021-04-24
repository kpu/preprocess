#ifndef PREPROCESS_PARALLEL__
#define PREPROCESS_PARALLEL__

#include "util/file_stream.hh"
#include "util/file_piece.hh"

#include <iostream>

#include <stdint.h>

template <class Pass> int FilterParallel(int argc, char **argv) {
  uint64_t input = 0, output = 0;
  if (argc == 1) {
    Pass pass;
    util::StringPiece line;
    util::FilePiece in(0, NULL, &std::cerr);
    util::FileStream out(1);
    while (true) {
      try {
        line = in.ReadLine();
      } catch (const util::EndOfFileException &e) { break; }
      ++input;
      if (pass(line)) {
        out << line << '\n';
        ++output;
      }
    }
  } else if (argc == 5) {
    Pass pass0, pass1;
    util::StringPiece line0, line1;
    util::FilePiece in0(argv[1], &std::cerr), in1(argv[2]);
    util::FileStream out0(util::CreateOrThrow(argv[3])), out1(util::CreateOrThrow(argv[4]));
    while (true) {
      try {
        line0 = in0.ReadLine();
      } catch (const util::EndOfFileException &e) { break; }
      line1 = in1.ReadLine();
      ++input;
      if (pass0(line0) && pass1(line1)) {
        out0 << line0 << '\n';
        out1 << line1 << '\n';
        ++output;
      }
    }
    try {
      line1 = in1.ReadLine();
      std::cerr << "Input is not balaced: " << argv[2] << " has " << line1 << std::endl;
      return 2;
    } catch (const util::EndOfFileException &e) {}
  } else {
    std::cerr << 
      "To filter one file, run\n" << argv[0] << " <stdin >stdout\n"
      "To filter parallel files, run\n" << argv[0] << " in0 in1 out0 out1\n";
    return 1;
  }
  std::cerr << "Kept " << output << " / " << input << " = " << (static_cast<float>(output) / static_cast<float>(input)) << std::endl;
  return 0;
}

#endif
