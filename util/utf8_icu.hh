/* Utilities for UTF-8 that require ICU.  */

#ifndef UTIL_UTF8_ICU
#define UTIL_UTF8_ICU

#include "util/string_piece.hh"

#include <exception>
#include <string>

#include <unicode/utypes.h>

U_NAMESPACE_BEGIN
class UnicodeString;
U_NAMESPACE_END

namespace util {

class NormalizeException : public std::exception {
  public:
    NormalizeException(const StringPiece &original, UErrorCode code) throw();
    ~NormalizeException() throw() {}

    const char *what() const throw() { return what_.c_str(); }

  private:
    std::string original_;

    std::string what_;
};


class ICUStupidlyUses32BitIntegersException : public std::exception {
  public:
    ~ICUStupidlyUses32BitIntegersException();
    const char *what() const throw();
};

// TODO: Implement these in a way that doesn't botch Turkish.
void ToLower(const StringPiece &in, std::string &out);

void Normalize(const U_ICU_NAMESPACE::UnicodeString &in, U_ICU_NAMESPACE::UnicodeString &out);
void Normalize(const StringPiece &in, std::string &out);

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

/* Technically Flatten could be done without ICU but then it's only used in process_unicode that wants UnicodeString */
class FlattenData;

class Flatten {
  public:
    explicit Flatten(const StringPiece &language);

    void Apply(const StringPiece &in, std::string &out) const;
    void Apply(const U_ICU_NAMESPACE::UnicodeString &in, U_ICU_NAMESPACE::UnicodeString &out) const;

  private:
    const FlattenData &data_;
};

} // namespace util

#endif // UTIL_UTF8_ICU
