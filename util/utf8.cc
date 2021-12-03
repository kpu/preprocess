#include "util/utf8.hh"

#include "util/string_piece.hh"

namespace util {

NotUTF8Exception::NotUTF8Exception(const StringPiece &) throw() {}

NotUTF8Exception::~NotUTF8Exception() throw() {}

bool IsUTF8(const StringPiece &str) {
  try {
    for (char32_t character : DecodeUTF8Range(str)) {
      (void)character; /*unused variable */
    }
    return true;
  } catch (const NotUTF8Exception &) {
    return false;
  }
}

} // namespace util
