/* Utilities for UTF-8.  */

#ifndef UTIL_UTF8__
#define UTIL_UTF8__

#include "util/string_piece.hh"

#include <exception>
#include <string>

#include <unicode/utypes.h>

U_NAMESPACE_BEGIN
class UnicodeString;
U_NAMESPACE_END

namespace utf8 {

class NormalizeException : public std::exception {
  public:
    NormalizeException(const UnicodeString &original, UErrorCode code) throw();
    ~NormalizeException() throw() {}

    const char *what() const throw() { return what_.c_str(); }

  private:
    std::string original_;

    std::string what_;
};

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

class UnsupportedLanguageException : public std::exception {
  public:
    explicit UnsupportedLanguageException(const StringPiece &language) throw();
    ~UnsupportedLanguageException() throw() {}

    const char *what() const throw() { return what_.c_str(); }
    
    const std::string &Language() const { return language_; }

  private:
    std::string language_;
    std::string what_;
};

bool IsPunctuation(const StringPiece &text) throw(NotUTF8Exception);

// TODO: Implement these in a way that doesn't botch Turkish.
void ToLower(const UnicodeString &in, UnicodeString &out);
void ToLower(const StringPiece &in, std::string &out) throw(NotUTF8Exception);

void Normalize(const UnicodeString &in, UnicodeString &out) throw(NotUTF8Exception, NormalizeException);
void Normalize(const StringPiece &in, std::string &out) throw(NotUTF8Exception, NormalizeException);

class FlattenData;

class Flatten {
  public:
    explicit Flatten(const StringPiece &language) throw(UnsupportedLanguageException);

    void Apply(const UnicodeString &in, UnicodeString &out) const throw(NotUTF8Exception);
    void Apply(const StringPiece &in, std::string &out) const throw (NotUTF8Exception);

  private:
    const FlattenData &data_;
};

} // namespace utf8

#endif
