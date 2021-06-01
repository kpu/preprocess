#include "util/utf8.hh"
#include "util/utf8_icu.hh"

#define BOOST_TEST_MODULE UTF8Test
#include <boost/test/unit_test.hpp>

#define CHECK_LOWER(ref, from) { \
  std::string out; \
  ToLower(from, out); \
  BOOST_CHECK_EQUAL(ref, out); \
}

#define CHECK_NORMALIZE(ref, from) { \
  std::string out; \
  Normalize(from, out); \
  BOOST_CHECK_EQUAL(ref, out); \
}

#define CHECK_FLATTEN(ref, from, language) { \
  Flatten flat(language); \
  std::string out; \
  flat.Apply(from, out); \
  BOOST_CHECK_EQUAL(ref, out); \
}

namespace util {
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

BOOST_AUTO_TEST_CASE(FailLarge) {
  StringPiece large(0, 1ULL << 32);
  std::string out;
  BOOST_CHECK_THROW(ToLower(large, out), ICUStupidlyUses32BitIntegersException);
}

BOOST_AUTO_TEST_CASE(IsUTF8Test) {
  BOOST_CHECK(IsUTF8("…œÆ5ôÆÐØôæðø"));
  BOOST_CHECK(!IsUTF8("…œ\xaaÆ5œÆ5ôÆÐØôæðø"));
}

BOOST_AUTO_TEST_CASE(Iterator) {
  DecodeUTF8Range range("\ufeffﬁ«🤦a");
  DecodeUTF8Iterator i = range.begin();
  BOOST_CHECK(i != range.end());
  BOOST_CHECK(!range.end());
  BOOST_CHECK_EQUAL(0xfeff, *i++);
  BOOST_CHECK_EQUAL(0xFB01, *i++);
  BOOST_CHECK_EQUAL(0xAB, *i++);
  BOOST_CHECK_EQUAL(0x1F926, *i++);
  BOOST_CHECK_EQUAL('a', *i++);
  BOOST_CHECK(!i);
  BOOST_CHECK(i == range.end());
}

/* This has been tested but it uses > 2 GB virtual memory so isn't enabled by default. */
/* BOOST_AUTO_TEST_CASE(LargeIsUTF8) {
  const size_t kBufferSize = (1ULL << 32) + 30ULL;
  std::vector<char> buffer(kBufferSize);
  StringPiece big(&*buffer.begin(), kBufferSize);
  BOOST_CHECK(IsUTF8(big));
  buffer[0] = 129;
  BOOST_CHECK(!IsUTF8(big));
  buffer[0] = 0;
  buffer[1ULL << 32] = 129;
  BOOST_CHECK(!IsUTF8(big));
}*/


} // namespace
} // namespace util
