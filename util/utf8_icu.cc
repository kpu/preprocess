#include "util/utf8_icu.hh"
#include "util/utf8.hh"

#include "util/murmur_hash.hh"
#include "util/string_piece.hh"

#include <unicode/normlzr.h>
#include <unicode/ucasemap.h>
#include <unicode/uchar.h>
#include <unicode/unistr.h>
#include <unicode/utf8.h>
#include <unicode/utypes.h>

#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

using U_ICU_NAMESPACE::UnicodeString;

namespace util {

NormalizeException::NormalizeException(const StringPiece &original, UErrorCode code) throw() 
  : original_(original.data(), original.length()) {
  what_ = "Normalization of '";
  what_ += original_;
  what_ += "' failed:";
  what_.append(u_errorName(code));
}

ICUStupidlyUses32BitIntegersException::~ICUStupidlyUses32BitIntegersException() {}
const char *ICUStupidlyUses32BitIntegersException::what() const throw() {
  return "ICU uses int32_t for string sizes but this string was longer than 2^31-1.  TODO: write a loop around it.";
}

namespace {

const size_t kInt32Max = 2147483647ULL;

void TODOLoopFor32Bit(const StringPiece &str) {
  if (str.size() > kInt32Max) throw ICUStupidlyUses32BitIntegersException();
}

} // namespace

namespace {
class CaseMapWrap {
  public:
    CaseMapWrap() : case_map_(NULL) {
      UErrorCode err_csm = U_ZERO_ERROR;
      case_map_ = ucasemap_open(NULL, 0, &err_csm);
      if (U_FAILURE(err_csm)) {
        std::cerr << "Failed to initialize case map." << std::endl;
        abort();
      }
    }

    ~CaseMapWrap() {
      if (case_map_) ucasemap_close(case_map_);
    }

    const UCaseMap *Get() const {
      return case_map_;
    }

  private:
    UCaseMap *case_map_;
};

} // namespace

void ToLower(const StringPiece &in, std::string &out) {
  static const CaseMapWrap wrap;
  TODOLoopFor32Bit(in);
  while (true) {
    UErrorCode err_lower = U_ZERO_ERROR;
    size_t need = ucasemap_utf8ToLower(wrap.Get(), &out[0], out.size(), in.data(), in.size(), &err_lower);
    if (err_lower == U_BUFFER_OVERFLOW_ERROR) {
      // Hopefully ensure convergence.
      out.resize(std::max(out.size(), need) + 10);
    } else if (U_FAILURE(err_lower)) {
      throw NotUTF8Exception(in);
    } else if (need > out.size()) {
      out.resize(need + 10);
    } else {
      out.resize(need);
      return;
    }
  }
}

void Normalize(const UnicodeString &in, UnicodeString &out) {
  UErrorCode errorcode = U_ZERO_ERROR;
  U_ICU_NAMESPACE::Normalizer::normalize(in, UNORM_NFKC, 0, out, errorcode);
  if (U_FAILURE(errorcode)) {
    std::string failed;  
    in.toUTF8String(failed);
    throw NormalizeException(failed, errorcode);
  }
}

void Normalize(const StringPiece &in, std::string &out) {
  TODOLoopFor32Bit(in);
  U_ICU_NAMESPACE::StringPiece icupiece(in.data(), in.size());
  UnicodeString asuni(UnicodeString::fromUTF8(icupiece));
  if (asuni.isBogus()) throw NotUTF8Exception(in);
  UnicodeString normalized;
  Normalize(asuni, normalized);
  out.clear();
  normalized.toUTF8String(out);
}

struct FlattenData {
  struct LongReplace {
    LongReplace(const UnicodeString &from_suffix_in, const UnicodeString &to_in, bool right_boundary_in) : from_suffix(from_suffix_in), to(to_in), right_boundary(right_boundary_in) {}
    // from except the first character
    UnicodeString from_suffix;
    UnicodeString to;
    bool right_boundary;
  };
  struct Start {
    // longer matches beginning with the same character.  
    std::vector<LongReplace> longer;
    // Fallback if nothing in longer matches.
    UnicodeString character;
  };
  std::unordered_map<UChar32, Start> starts;
};

namespace {

struct ReplaceRule {
  const char *from, *to;
};

template <unsigned rulecount> void AddToFlatten(const ReplaceRule (&replace)[rulecount], FlattenData &out, bool right_boundary = false) {
  FlattenData::Start default_start;
  for (const ReplaceRule *i = replace; i != replace + rulecount; ++i) {
    const char *to = i->to;
    UnicodeString to_unicode;
    Normalize(UnicodeString::fromUTF8(to), to_unicode);

    const char *from = i->from;
    int32_t offset = 0;
    int32_t length = strlen(from);
    UChar32 from_char;
    U8_NEXT(from, offset, length, from_char);
    if (from_char < 0) throw NotUTF8Exception(from);
    if (offset == length) {
      out.starts[from_char].character = to_unicode;
    } else {
      default_start.character.setTo(from_char);
      out.starts.insert(std::make_pair(from_char, default_start)).first->second.longer.push_back(FlattenData::LongReplace(UnicodeString::fromUTF8(from + offset), to_unicode, right_boundary));
    }
  }
}

const ReplaceRule kGeneralReplace[] = {
  {"æ", "ae"},
  {"Æ", "Ae"},
  {"Œ", "Oe"},
  {"œ", "oe"},
  {"ﬁ", "fi"},
  {"\u00A0", " "},
  {"\u2028", " "},
  {"…", "..."},
  {"）", ")"},
  {"（", "("},
  // These long hyphens will turn into single space-separated hyphens.  
  {"–", "--"},
  {"—", "--"},
  // Chris Dyer t2.perl
  {"●", "*"},
  {"•", "*"},
  {"·", "*"},
  {"& quot ;", "\""},
  {"& lt ;", "<"},
  {"& gt ;", ">"},
  {"& squot ;", "'"},
  {"& amp ;", "&"},
};

const ReplaceRule kReplaceWithQuote[] = {
  {"``", "\""},
  {"''", "\""},
  {"«", "\""},
  {"»", "\""},
  {"”", "\""},
  {"“", "\""},
  {"″", "\""},
  {"„", "\""},
  {"’", "'"},
  {"‘", "'"},
  {"′", "'"},
  {"´", "'"},
  // Added after WMT 10
  {"‹", "'"},
  {"›", "'"},
  {"`", "'"},
};

const ReplaceRule kReplaceForEnglishRightBoundary[] = {
  {"' s", "'s"},
  {" - year - old", " -year-old"},
  {" - years - old", " -years-old"},
};

const ReplaceRule kReplaceForEnglish[] = {
  // Gigaword has weird braces after numbers.  
  {"0{", "0"},
  {"1{", "1"},
  {"2{", "2"},
  {"3{", "3"},
  {"4{", "4"},
  {"5{", "5"},
  {"6{", "6"},
  {"7{", "7"},
  {"8{", "8"},
  {"9{", "9"},
};

const ReplaceRule kReplaceForFrench[] = {
  {"``", "«"},
  {"''", "»"},
  {"”", "»"},
  // This can also be a close quote (as used in Czech) but hopefully not in French.  
  {"“", "«"},
  {"″", "»"},
  {"„", "«"},
  {"’", "›"},
  {"‘", "‹"},
  // Formally this a a prime, not a quote.  
  {"′", "'"},
  // This is acute accent, not a quote.
  //{"´", "'"},
};

struct StringPieceHash {
  size_t operator()(StringPiece str) const {
    return util::MurmurHashNative(str.data(), str.size());
  }
};

struct AllFlattenData {
  public:
    AllFlattenData() {
      // Quoting http://www.boost.org/doc/libs/1_42_0/doc/html/boost/unordered_map.html on insert: "Pointers and references to elements are never invalidated."
      FlattenData &english = lang_map_["en"];
      FlattenData &french = lang_map_["fr"];
      FlattenData &german = lang_map_["de"];
      FlattenData &spanish = lang_map_["es"];
      FlattenData &czech = lang_map_["cs"];

      // Get the general stuff.
      AddToFlatten(kGeneralReplace, english);
      french = english;
      czech = english;
      german = english;
      spanish = english;

      AddToFlatten(kReplaceWithQuote, english);
      AddToFlatten(kReplaceWithQuote, german);
      AddToFlatten(kReplaceWithQuote, spanish);

      AddToFlatten(kReplaceForEnglishRightBoundary, english, true);
      AddToFlatten(kReplaceForEnglish, english);
      AddToFlatten(kReplaceForFrench, french);
      // TODO: Czech quotes.  
    }

    const FlattenData &ForLanguage(StringPiece language) const {
      std::unordered_map<StringPiece, FlattenData, StringPieceHash>::const_iterator i = lang_map_.find(language);
      if (i == lang_map_.end()) throw UnsupportedLanguageException(language);
      return i->second;
    }

  private:
    std::unordered_map<StringPiece, FlattenData, StringPieceHash> lang_map_;
};

} // namespace

UnsupportedLanguageException::UnsupportedLanguageException(const StringPiece &language) throw() : language_(language.data(), language.size()) {
  what_ = "Unsupported language: ";
  what_ += language_;
}

namespace {
const FlattenData &LookupFlatten(const StringPiece &language) {
  static const AllFlattenData kAllFlattenData;
  return kAllFlattenData.ForLanguage(language);
}
} // namespace

Flatten::Flatten(const StringPiece &language) : data_(LookupFlatten(language)) {}

void Flatten::Apply(const UnicodeString &in, UnicodeString &out) const {
  out.truncate(0);

  for (int32_t i = 0; i < in.length();) {
    UChar32 character = in.char32At(i);
    assert(character > 0);
    std::unordered_map<UChar32, FlattenData::Start>::const_iterator entry(data_.starts.find(character));
    if (entry != data_.starts.end()) {
      const FlattenData::Start &start = entry->second;
      std::vector<FlattenData::LongReplace>::const_iterator j(start.longer.begin());
      for (; j != start.longer.end(); ++j) {
        const UnicodeString &from_suffix = j->from_suffix;
        int32_t ending = i + 1 + from_suffix.length();
        if (!in.compare(i + 1, from_suffix.length(), from_suffix) && 
            (!j->right_boundary || (in.length() == ending) || u_isspace(in.char32At(ending)))) {
          // Found longer match.
          out.append(j->to);
          i += from_suffix.length();
          break;
        }
      }
      // Fallback on character
      if (j == start.longer.end()) out.append(start.character);
      ++i;
    } else {
      out.append(character);
      ++i;
    }
  }
}

void Flatten::Apply(const StringPiece &in, std::string &out) const {
  TODOLoopFor32Bit(in);
  U_ICU_NAMESPACE::StringPiece icupiece(in.data(), in.size());
  UnicodeString asuni(UnicodeString::fromUTF8(icupiece));
  if (asuni.isBogus()) throw NotUTF8Exception(in);
  UnicodeString normalized;
  Apply(asuni, normalized);
  out.clear();
  normalized.toUTF8String(out);
}

} // namespace util
