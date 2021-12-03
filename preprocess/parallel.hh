#ifndef PREPROCESS_PARALLEL__
#define PREPROCESS_PARALLEL__

#include "util/file_stream.hh"
#include "util/file_piece.hh"

#include <iostream>
#include <string>
#include <vector>

#include <stdint.h>

namespace preprocess {

template <class Pass, class... PassArguments> int FilterParallel(const std::vector<std::string> &files, PassArguments&&... pass_construct) {
  uint64_t input = 0, output = 0;
  if (files.empty()) {
    Pass pass(std::forward<PassArguments>(pass_construct)...);
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
  } else if (files.size() == 4) {
    Pass pass0(std::forward<PassArguments>(pass_construct)...), pass1(std::forward<PassArguments>(pass_construct)...);
    util::StringPiece line0, line1;
    util::FilePiece in0(files[0].c_str(), &std::cerr), in1(files[1].c_str());
    util::FileStream out0(util::CreateOrThrow(files[2].c_str())), out1(util::CreateOrThrow(files[3].c_str()));
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
      std::cerr << "Input is not balaced: " << files[1] << " has " << line1 << std::endl;
      return 2;
    } catch (const util::EndOfFileException &e) {}
  } else {
    std::cerr << 
      "To filter from stdin to stdout, run without an argument.\n"
      "To filter parallel files, run in0 in1 out0 out1\n";
    return 1;
  }
  std::cerr << "Kept " << output << " / " << input << " = " << (static_cast<float>(output) / static_cast<float>(input)) << std::endl;
  return 0;
}

} // namespace preprocess
#endif
