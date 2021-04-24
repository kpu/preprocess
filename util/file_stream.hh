/* Like std::ofstream but without being incredibly slow.  Backed by a raw fd that it owns.
 * Supports most of the built-in types except for long double.
 */
#ifndef UTIL_FILE_STREAM_H
#define UTIL_FILE_STREAM_H

#include "util/buffered_stream.hh"
#include "util/file.hh"

#include <stdint.h>

namespace util {

typedef BufferedStream<FileWriter> FileStream;

} // namespace

#endif
