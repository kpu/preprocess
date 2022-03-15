#include "parallel.hh"
#include "fields.hh"
#include "../util/file_stream.hh"
#include "../util/file_piece.hh"

#include <boost/program_options.hpp>
#include <boost/program_options/positional_options.hpp>

#include <algorithm>
#include <iostream>
#include <numeric>
#include <string>
#include <vector>

#include <stdint.h>
#include <string.h>
#include <unicode/uchar.h>
#include <unicode/uscript.h>

namespace preprocess {
namespace {

struct Options {
  std::vector<FieldRange> key_fields;
  char delim;
  std::vector<std::string> files;

  size_t min_chars;
  float max_common_inherited;
  float min_punct;
  size_t min_punct_sample_size;
  size_t character_run;

  std::vector<UScriptCode> scripts;
  float min_scripts;
};

void ScriptStringsToCodes(const std::vector<std::string> &str, std::vector<UScriptCode> &code) {
  for (const std::string &s : str) {
    UErrorCode err = U_ZERO_ERROR;
    int32_t required = uscript_getCode(s.c_str(), nullptr, 0, &err);
    if (err != U_BUFFER_OVERFLOW_ERROR) {
      if (err == U_ZERO_ERROR) {
        std::cerr << "Could not map \"" << s << "\" to a script.  See https://unicode-org.github.io/icu-docs/apidoc/released/icu4c/uscript_8h.html" << std::endl;
        abort();
      }
      std::cerr << "Got unexpected error " << err << " while interpreting script code " << s << std::endl;
      abort();
    }
    code.resize(code.size() + required);
    err = U_ZERO_ERROR;
    uscript_getCode(s.c_str(), &code[code.size() - required], required, &err);
    if (U_FAILURE(err)) {
      std::cerr << "Error " << err << " trying to understand script " << s << std::endl;
      abort();
    }
  }
  std::sort(code.begin(), code.end());
  code.resize(std::unique(code.begin(), code.end()) - code.begin());
}

void ParseArgs(int argc, char *argv[], Options &out) {
  namespace po = boost::program_options;
  po::options_description desc("Cleaning settings");
  std::string fields;
  std::vector<std::string> scripts;

  desc.add_options()
    ("help,h", po::bool_switch(), "Show this help message")
    ("fields,f", po::value(&fields)->default_value("1-"), "Fields to use for key like cut -f")
    ("delim,d", po::value(&out.delim)->default_value('\t'), "Field delimiter")
    ("parallel,p", po::value(&out.files)->multitoken(), "Filter parallel data using four files: in_en in_fr out_en out_fr")
    ("min-chars", po::value(&out.min_chars)->default_value(30), "Remove lines with less than this many UTF-8 characters")
    ("character-run", po::value(&out.character_run)->default_value(5), "Remove lines that consecutively repeat the same non-space character more than this many times")
    ("max-common-inherited", po::value(&out.max_common_inherited)->default_value(0.2f), "Remove lines with more than this fraction of characters belonging to common and inherited scripts classes, excluding whitepsace")
    ("min-punct", po::value(&out.min_punct)->default_value(0.01f), "Remove lines with less than this fraction of punctuation if they are also longer than --min-punct-sample-size characters")
    ("min-punct-sample-size", po::value(&out.min_punct_sample_size)->default_value(200), "Enforce the minimum punctuation requirement only for lines longer than this many characters")
    ("scripts", po::value(&scripts)->multitoken(), "Require these scripts (in addition to common and inherited scripts) occupy at least --min-scripts of the line; an empty list disables this filter")
    ("min-scripts", po::value(&out.min_scripts)->default_value(0.9f), "Fraction of characters (excluding common and inherited) that should be the scripts listed in --scripts");
  po::positional_options_description pd;
  pd.add("parallel", -1);

  po::variables_map vm;
  po::store(po::command_line_parser(argc, argv).options(desc).positional(pd).run(), vm);
  if (vm["help"].as<bool>() || (!out.files.empty() && out.files.size() != 4)) {
    std::cerr <<
      "Simple rule-based cleaning at the line level.  Removes:\n"
      "  Invalid UTF-8\n"
      "  Control characters (except tab and newline).\n"
      "  Lines shorter than --min-chars as measured in codepoints.\n"
      "  Consecutive runs of --character-run or more of the same non-space character\n" 
      "  Common and Inherited Unicode script characters (like numbers) too common\n"
      "  Too much or too little punctuation\n"
      "  Too little in the expected script\n" <<
      desc <<
      "Clean lines in a file: " << argv[0] << " <in >out\n"
      "Clean parallel data, removing if either side is unclean " << argv[0] << " -p in_en in_fr out_en out_fr\n";
    exit(1);
  }
  po::notify(vm);

  ParseFields(fields.c_str(), out.key_fields);
  DefragmentFields(out.key_fields);

  ScriptStringsToCodes(scripts, out.scripts);
}

class SimpleCleaningFilter {
  public:
    explicit SimpleCleaningFilter(const Options &options) : options_(options) {}
  
    bool operator()(const util::StringPiece &line) const {
      int32_t offset = 0;
      int32_t length = static_cast<int32_t>(line.size());
      size_t counts[USCRIPT_CODE_LIMIT];
      memset(counts, 0, sizeof(counts));
      size_t punct = 0, spaces = 0;
      UChar32 previous = 0;
      size_t previous_run = 0;
      while (offset < length) {
        UChar32 character;
        U8_NEXT(line.data(), offset, length, character);
        // Avoid bad unicode and control characters
        if (character < 32 && character != '\t' && character != '\r') {
          return false;
        }
        UErrorCode err = U_ZERO_ERROR;
        UScriptCode script = uscript_getScript(character, &err);
        if (U_FAILURE(err) || script == USCRIPT_INVALID_CODE) {
          return false;
        }
        ++counts[script];
        if (u_ispunct(character)) {
          ++punct;
        }
        if (u_isspace(character)) {
          ++spaces;
        }
        if (previous == character) {
          // Runs of >= n of the same non-space character
          if (++previous_run >= options_.character_run && !u_isspace(character)) {
            return false;
          }
        } else {
          previous = character;
          previous_run = 1;
        }
      }
      size_t characters = std::accumulate(counts, counts + USCRIPT_CODE_LIMIT, 0);
      // Less than n characters
      if (characters < options_.min_chars) {
        return false;
      }
      // More than x% common or inherited script, excluding spaces.
      // Note: spaces are language-specific so we don't require a particular frequency here.
      size_t common_inherited = counts[USCRIPT_INHERITED] + counts[USCRIPT_COMMON] - spaces;
      if (static_cast<float>(common_inherited) > options_.max_common_inherited * static_cast<float>(characters)) {
        return false;
      }
      // At least y% punctuation
      if (characters > options_.min_punct_sample_size && punct < options_.min_punct * characters) {
        return false;
      }

      if (!options_.scripts.empty()) {
        size_t after_common_inherited = characters - counts[USCRIPT_INHERITED] - counts[USCRIPT_COMMON];
        size_t in_script = 0;
        for (UScriptCode s : options_.scripts) {
          in_script += counts[s];
        }
        if (static_cast<float>(in_script) / static_cast<float>(after_common_inherited) < options_.min_scripts) {
          return false;
        }
      }

      return true;
    }

  protected:
    Options options_;
};

// This processes field information not just a full string.
class SimpleCleaningFilterFields : public SimpleCleaningFilter {
  public:
    explicit SimpleCleaningFilterFields(const Options &options) : SimpleCleaningFilter(options) {}

    bool operator()(const util::StringPiece &line) const {
      return IndividualFields(line, options_.key_fields, options_.delim, *static_cast<const SimpleCleaningFilter*>(this));
    }
};

} // namespace
} // namespace preprocess

int main(int argc, char *argv[]) {
  preprocess::Options options;
  ParseArgs(argc, argv, options);
  std::vector<std::string> args;
  for (int i = 1; i < argc; ++i) {
    args.push_back(argv[i]);
  }
  return preprocess::FilterParallel<preprocess::SimpleCleaningFilterFields>(options.files, options);
}
