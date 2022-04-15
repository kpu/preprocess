#include "xxhash.h"

#include "warcreader.hh"
#include "util/file_stream.hh"
#include "util/murmur_hash.hh"
#include "util/tokenize_piece.hh"

#include <iostream>
#include <unordered_map>
#include <vector>

#include <string.h>

class MatchException : public util::Exception {};

struct Extract {
  // Raw line number in the WET
  uint64_t paragraph_number;
  // XXH3_64bits_withSeed of line in the WET
  uint64_t paragraph_digest;
  // XXH3_64bits_withSeed after sentence splitting / preprocesing the line.
  uint64_t sentence_digest;
  float lid_score;
  float laser_score;
  // File to write to (side of a parallel corpus)
  util::FileStream *out;
  // Line number in final parallel corpus.
  uint64_t out_line_number;
};

void ProcessExtract(const Extract &extract, util::StringPiece line) {
  if (!line.empty() && line[line.size() - 1] == '\r') {
    line.remove_suffix(1);
  }
  XXH64_hash_t hash = XXH3_64bits_withSeed(line.data(), line.size(), 0);
  UTIL_THROW_IF(hash != extract.paragraph_digest, MatchException, "Paragraph '" << line << "' hashed to " << hash << " but the metadata expected " << extract.paragraph_digest);
  (*extract.out) << extract.out_line_number << ' ' << extract.sentence_digest << ' ' << extract.lid_score << ' ' << extract.laser_score << ' ' << line << '\n';
}

struct SHA1 {
  char base32[32];
  bool operator==(const SHA1 &other) const {
    return !memcmp(base32, other.base32, sizeof(base32));
  }
};

// TODO if we decode the base32 to a number this could just be the prefix.
namespace std {
template<> struct hash<SHA1> {
  size_t operator()(const SHA1 &in) const {
    return util::MurmurHashNative(in.base32, sizeof(in.base32));
  }
};
} // namespace std

class Retrieve {
  public:
    void Add(util::StringPiece sha1, const Extract &extract) {
      UTIL_THROW_IF(sha1.size() != 32, MatchException, "Expected 32-character hash but got '" << sha1 << "' with size " << sha1.size());
      const SHA1 &key = *reinterpret_cast<const SHA1*>(sha1.data());
      std::vector<Extract> &extracts = map_[key];
      UTIL_THROW_IF2(!extracts.empty() && extracts.back().paragraph_number > extract.paragraph_number, "Metadata should be sorted by paragraph number in each document");
      extracts.push_back(extract);
    }
    const std::vector<Extract> &Lookup(util::StringPiece sha1) const {
      UTIL_THROW_IF(sha1.size() != 32, MatchException, "Expected 32-character hash but got '" << sha1 << "' with size " << sha1.size());
      const SHA1 &key = *reinterpret_cast<const SHA1*>(sha1.data());
      std::unordered_map<SHA1, std::vector<Extract> >::const_iterator it = map_.find(key);
      if (it == map_.end()) return empty_;
      return it->second;
    }

  private:
    const std::vector<Extract> empty_;
    std::unordered_map<SHA1, std::vector<Extract> > map_;
};

// Extract SHA1 from header, leave at line
util::StringPiece FindSHA1(util::TokenIter<util::SingleCharacter, false> &line) {
  const util::StringPiece kBlockDigest("WARC-Block-Digest: sha1:");
  // Header through SHA1
  for (++line; ; ++line) {
    UTIL_THROW_IF(!line, MatchException, "Missing end of header");
    if (line->starts_with(kBlockDigest)) {
      UTIL_THROW_IF((*line)[line->size() - 1] != '\r', MatchException, "Expected carriage return in WARC.");
      util::StringPiece ret(line->substr(kBlockDigest.size()));
      ret.remove_suffix(1);
      return ret;
    }
    UTIL_THROW_IF(line->empty(), MatchException, "No digest");
  }
}

void MatchLines(util::TokenIter<util::SingleCharacter, false> &line, const std::vector<Extract> &extracts) {
  assert(!extracts.empty());
  uint64_t line_counter = 0;
  std::vector<Extract>::const_iterator extract = extracts.begin();
  for (++line; line; ++line_counter, ++line) {
    while (line_counter == extract->paragraph_number) {
      ProcessExtract(*extract, *line);
      if (++extract == extracts.end()) {
        return;
      }
    }
  }
  UTIL_THROW(MatchException, "Paragraph number " << extract->paragraph_number << " exceeds size.")
}

void ProcessWARC(warc2text::WARCReader &reader, Retrieve &retrieve) {
  std::string str;
  UTIL_THROW_IF(!reader.getRecord(str), MatchException, "Missing warcinfo");
  const char kStart[] = "WARC/1.0\r\nWARC-Type: warcinfo\r\n";
  UTIL_THROW_IF(strncmp(str.c_str(), kStart, sizeof(kStart) - 1), MatchException, "WARC does not begin with warcinfo");
  while (reader.getRecord(str)) {
    util::TokenIter<util::SingleCharacter, false> line(str, '\n');
    UTIL_THROW_IF(!line, MatchException, "Blank document");
    UTIL_THROW_IF(*line != "WARC/1.0\r", MatchException, "Expected WARC/1.0 header but got `" << *line << '\'');
    util::StringPiece sha1 = FindSHA1(line);
    const std::vector<Extract> &extracts = retrieve.Lookup(sha1);
    if (extracts.empty()) {
      continue;
    }
    // Consume rest of the header.
    for (++line; ; ++line) {
      UTIL_THROW_IF(!line, MatchException, "Missing end of header");
      if (line->size() == 1 && (*line)[0] == '\r') break;
    }
    MatchLines(line, extracts);
  }
}

int main() {
  Retrieve retrieve;
  util::FileStream file(1);
  retrieve.Add("VGOLEON3MW757B2FHQ5SMBY6EQQ5QHQW", Extract {36, 16658339799244853606ULL, 16658339799244853606ULL, 0.80266, 1.0559655, &file, 814970});
  warc2text::WARCReader reader("/dev/stdin");
  ProcessWARC(reader, retrieve);
}
