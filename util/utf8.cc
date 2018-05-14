#include "util/utf8.hh"

#include "util/string_piece.hh"

#include <unicode/ucasemap.h>
#include <unicode/uchar.h>
#include <unicode/unistr.h>
#include <unicode/utf8.h>
#include <unicode/utypes.h>

#include <cstdlib>
#include <iostream>
#include <string>

namespace utf8 {

// Could be more efficient, but I'm not terribly worried about that.
NotUTF8Exception::NotUTF8Exception(const StringPiece &original) throw()
  : original_(original.data(), original.length()),
    what_(std::string("Bad UTF-8: ") + original_) {}

// Could be more efficient, but I'm not terribly worried about that.
NotUTF8Exception::NotUTF8Exception(const StringPiece &original, UErrorCode code) throw()
  : original_(original.data(), original.length()),
    what_(std::string("Bad UTF-8: '") + original_) {
      what_.append("': ");
      what_.append(u_errorName(code));
    }

namespace {
class CaseMapWrap {
  public:
    CaseMapWrap() : case_map_(NULL) {}

    void Init() {
      UErrorCode err_csm = U_ZERO_ERROR;
      case_map_ = ucasemap_open(NULL, 0, &err_csm);
      if (U_FAILURE(err_csm)) {
        std::cerr << "Failed to initialize case map." << std::endl;
        std::abort();
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

CaseMapWrap kCaseMap;

} // namespace

void Init() {
  kCaseMap.Init();
}

void ToLower(const StringPiece &in, std::string &out) throw(NotUTF8Exception) {
  const UCaseMap *csm = kCaseMap.Get();
  while (true) {
    UErrorCode err_lower = U_ZERO_ERROR;
    size_t need = ucasemap_utf8ToLower(csm, &out[0], out.size(), in.data(), in.size(), &err_lower);
    if (err_lower == U_BUFFER_OVERFLOW_ERROR) {
      // Hopefully ensure convergence.
      out.resize(std::max(out.size(), need) + 10);
    } else if (U_FAILURE(err_lower)) {
      throw NotUTF8Exception(in, err_lower);
    } else if (need > out.size()) {
      out.resize(need + 10);
    } else {
      out.resize(need);
      return;
    }
  }
}

} // namespace utf8
