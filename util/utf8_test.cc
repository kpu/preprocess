#include "util/utf8.hh"

#define BOOST_TEST_MODULE UTF8Test
#include <boost/test/unit_test.hpp>

#define CHECK_LOWER(ref, from) { \
  std::string out; \
  ToLower(from, out); \
  BOOST_CHECK_EQUAL(ref, out); \
}

#define CHECK_NORMALIZE(ref, from) { \
  std::string out; \
  utf8::Normalize(from, out); \
  BOOST_CHECK_EQUAL(ref, out); \
}

#define CHECK_FLATTEN(ref, from, language) { \
  Flatten flat(language); \
  std::string out; \
  flat.Apply(from, out); \
  BOOST_CHECK_EQUAL(ref, out); \
}

namespace utf8 {
namespace {

BOOST_AUTO_TEST_CASE(ASCII) {
  CHECK_LOWER("foo", "FOO");
  CHECK_LOWER("foobaz", "fooBAz");
}

BOOST_AUTO_TEST_CASE(Accents) {
  CHECK_LOWER("ôæðø", "ôÆÐØ");
}

BOOST_AUTO_TEST_CASE(Thorn) {
  CHECK_LOWER("þ", "Þ");
}

BOOST_AUTO_TEST_CASE(NormalizeASCII) {
  CHECK_NORMALIZE("foo", "foo");
}

// This is a valid letter in some languages
BOOST_AUTO_TEST_CASE(NormalizeAE) {
  CHECK_NORMALIZE("æ", "æ");
}

BOOST_AUTO_TEST_CASE(NormalizeFI) {
  CHECK_NORMALIZE("fi", "ﬁ");
}

BOOST_AUTO_TEST_CASE(NormalizeFive) {
  CHECK_NORMALIZE("5", "⁵");
}

BOOST_AUTO_TEST_CASE(FlattenEnglish) {
  CHECK_FLATTEN("\"foo bar\" '", "«foo bar» '", "en");
}

BOOST_AUTO_TEST_CASE(FlattenFrench) {
  CHECK_FLATTEN("«foo bar»", "``foo bar''", "fr");
}

BOOST_AUTO_TEST_CASE(FlattenBunch) {
  CHECK_FLATTEN("...oeAe\"'s ", "…œÆ''' s ", "en");
}

BOOST_AUTO_TEST_CASE(FlattenPossessive) {
  CHECK_FLATTEN("'s", "' s", "en");
  CHECK_FLATTEN("'s ", "' s ", "en");
  CHECK_FLATTEN("a's", "a' s", "en");
  CHECK_FLATTEN("a's ", "a' s ", "en");
  CHECK_FLATTEN("' sfoo", "' sfoo", "en");
  CHECK_FLATTEN("' sfoo ", "' sfoo ", "en");
}

} // namespace
} // namespace util
