#include "util/utf8.hh"

#include <boost/program_options/options_description.hpp>
#include <boost/program_options/parsers.hpp>
#include <boost/program_options/variables_map.hpp>

#include <unicode/unistr.h>
#include <unicode/ustream.h>

#include <algorithm>
#include <string>
#include <iostream>

using U_ICU_NAMESPACE::UnicodeString;

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
    ("language,l", po::value(&out.language)->default_value("en"), "Language (only applies to flatten)")
    ("lower", po::bool_switch(&out.lower)->default_value(false), "Convert to lowercase")
    ("flatten", po::bool_switch(&out.flatten)->default_value(false), "Canonicalize some characters for English")
    ("normalize", po::bool_switch(&out.normalize)->default_value(false), "Normalize Unicode format");
  po::variables_map vm;
  po::store(po::parse_command_line(argc, argv, desc), vm);
  po::notify(vm);
}
} // namespace

int main(int argc, char *argv[]) {
  Options opt;
  ParseArgs(argc, argv, opt);
  util::Flatten flatten(opt.language);
  std::string line, normalized;
  UnicodeString str[2];
  UnicodeString *cur = &str[0], *tmp = &str[1];
  while (getline(std::cin, line)) {
    *cur = UnicodeString::fromUTF8(line);
    if (opt.lower) {
      cur->toLower();
    }
    if (opt.flatten) {
      flatten.Apply(*cur, *tmp);
      std::swap(cur, tmp);
    }
    if (opt.normalize) {
      util::Normalize(*cur, *tmp);
      std::swap(cur, tmp);
    }
    std::cout << *str << '\n';
  }
}
