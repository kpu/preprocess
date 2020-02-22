#pragma once

#include "util/read_compressed.hh"

#include <string>

namespace preprocess {

class WARCReader {
  public:
    explicit WARCReader(int fd) : reader_(fd) {}

    bool Read(std::string &out);

  private:
    util::ReadCompressed reader_;

    std::string overhang_;
};

} // namespace preprocess
