/* Utilities for UTF-8.  */

#ifndef UTIL_UTF8
#define UTIL_UTF8

#include "util/string_piece.hh"

#include <exception>
#include <string>

namespace utf8 {

// This is what happens when you pass bad UTF8.  
class NotUTF8Exception : public std::exception {
  public:
    explicit NotUTF8Exception(const StringPiece &original) throw();
    NotUTF8Exception(const StringPiece &original, UErrorCode code) throw();
    ~NotUTF8Exception() throw() {}

    const char *what() const throw() { return what_.c_str(); }

    // The string you passed.
    const std::string &Original() const { return original_; }

  private:
    const std::string original_;

    std::string what_;
};

void Init();

// TODO: Implement these in a way that doesn't botch Turkish.
void ToLower(const StringPiece &in, std::string &out) throw(NotUTF8Exception);

} // namespace utf8

#endif // UTIL_UTF8
