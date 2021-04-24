/* Like std::ofstream but without being incredibly slow.  Backed by a raw fd.
 * Supports most of the built-in types except for long double.
 */
#ifndef UTIL_FILE_STREAM_H
#define UTIL_FILE_STREAM_H

#include "util/buffered_stream.hh"
#include "util/file.hh"

#include <stdint.h>

namespace util {

class FileWriter {
  public:
    explicit FileWriter(int out) : fd_(out) {}

    void write(const void *data, size_t amount) {
      WriteOrThrow(fd_, data, amount);
    }

    void flush() {
      FSyncOrThrow(fd_);
    }

  private:
    int fd_;
};

typedef BufferedStream<FileWriter> FileStream;

} // namespace

#endif
