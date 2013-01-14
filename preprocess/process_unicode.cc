#include "util/utf8.hh"

#include <boost/program_options/options_description.hpp>
#include <boost/program_options/parsers.hpp>
#include <boost/program_options/variables_map.hpp>

#include <unicode/unistr.h>
#include <unicode/ustream.h>

#include <string>
#include <iostream>

namespace {
struct Options {
  std::string language;
  bool lower;
  bool flatten;
  bool normalize;
};
void ParseArgs(int argc, char *argv[], Options &out) {
  namespace po = boost::program_options;
  po::options_description desc("Unicode treatment options");
  desc.add_options()
    ("language", po::value(&out.language)->default_value("en"), "Language (only applies to flatten)")
    ("lower", po::value(&out.lower)->default_value(false), "Convert to lowercase")
    ("flatten", po::value(&out.flatten)->default_value(false), "Canonicalize some characters for English")
    ("normalize", po::value(&out.normalize)->default_value(false), "Normalize Unicode format");
  po::variables_map vm;
  po::store(po::parse_command_line(argc, argv, desc), vm);
  po::notify(vm);
}
} // namespace

int main(int argc, char *argv[]) {
  Options opt;
  ParseArgs(argc, argv, opt);
  utf8::Flatten flatten(opt.language);
  std::string line, normalized;
  while (getline(std::cin, line)) {
    UnicodeString str(UnicodeString::fromUTF8(line));
    if (opt.lower) {
      str.toLower();
    }
    if (opt.flatten) {
      UnicodeString tmp = str;
      flatten.Apply(tmp, str);
    }
    if (opt.normalize) {
      UnicodeString tmp = str;
      utf8::Normalize(tmp, str);
    }
    std::cout << str << '\n';
  }
}
