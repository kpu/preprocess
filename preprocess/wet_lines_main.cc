#include "xxhash.h"

#include "warcstream.hh"
#include "util/file_stream.hh"
#include "util/murmur_hash.hh"
#include "util/tokenize_piece.hh"

#include <iostream>
#include <unordered_map>
#include <vector>

#include <curl/curl.h>
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
    typedef std::unordered_map<SHA1, std::vector<Extract> >::iterator Iterator;

    void Add(util::StringPiece sha1, const Extract &extract) {
      UTIL_THROW_IF(sha1.size() != 32, MatchException, "Expected 32-character hash but got '" << sha1 << "' with size " << sha1.size());
      const SHA1 &key = *reinterpret_cast<const SHA1*>(sha1.data());
      std::vector<Extract> &extracts = map_[key];
      UTIL_THROW_IF2(!extracts.empty() && extracts.back().paragraph_number > extract.paragraph_number, "Metadata should be sorted by paragraph number in each document");
      extracts.push_back(extract);
    }

    Iterator Lookup(util::StringPiece sha1) {
      UTIL_THROW_IF(sha1.size() != 32, MatchException, "Expected 32-character hash but got '" << sha1 << "' with size " << sha1.size());
      const SHA1 &key = *reinterpret_cast<const SHA1*>(sha1.data());
      return map_.find(key);
    }

    void Success(Iterator it) {
      map_.erase(it);
    }

    Iterator End() { return map_.end(); }

  private:
    const std::vector<Extract> empty_;
    std::unordered_map<SHA1, std::vector<Extract> > map_;
};

// Extract SHA1 from header, leave at line
util::StringPiece FindSHA1(util::TokenIter<util::SingleCharacter, false> &line) {
  const util::StringPiece kBlockDigest("WARC-Block-Digest: sha1:");
  // Header through SHA1
  for (; ; ++line) {
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

class DocumentCallback {
  public:
    explicit DocumentCallback(Retrieve &retrieve) : retrieve_(retrieve) {}

    void operator()(const std::string &document) {
      util::TokenIter<util::SingleCharacter, false> line(document, '\n');
      UTIL_THROW_IF(!line, MatchException, "Blank document");
      UTIL_THROW_IF(*line != "WARC/1.0\r", MatchException, "Expected WARC/1.0 header but got `" << *line << '\'');
      UTIL_THROW_IF(!++line, MatchException, "Nothing after WARC/1.0 header");
      if (*line == "WARC-Type: warcinfo\r") {
        return;
      }
      util::StringPiece sha1 = FindSHA1(line);
      Retrieve::Iterator it = retrieve_.Lookup(sha1);
      if (it == retrieve_.End()) return;
      const std::vector<Extract> &extracts = it->second;
      assert(!extracts.empty());
      // Consume rest of the header.
      for (++line; ; ++line) {
        UTIL_THROW_IF(!line, MatchException, "Missing end of header");
        if (line->size() == 1 && (*line)[0] == '\r') break;
      }
      MatchLines(line, extracts);
      retrieve_.Success(it);
    }
  private:
    Retrieve &retrieve_;
};

class CurlCallback {
  public:
    CurlCallback(Retrieve &retrieve) : document_(retrieve) {}

    size_t operator()(void *buffer, size_t length) {
      warc_.GiveBytes(static_cast<const char*>(buffer), length, document_);
      return length;
    }

    static size_t FunctionPtr(void *buffer, size_t /* one */, size_t nmemb, void *ptr) {
      return (*static_cast<CurlCallback*>(ptr))(buffer, nmemb);
    }

  private:
    preprocess::WARCStream warc_;
    DocumentCallback document_;
};

int main() {
  Retrieve retrieve;
  util::FileStream file(1);
  retrieve.Add("VGOLEON3MW757B2FHQ5SMBY6EQQ5QHQW", Extract {36, 16658339799244853606ULL, 16658339799244853606ULL, 0.80266, 1.0559655, &file, 814970});
  CurlCallback callback(retrieve);
  CURL *curl = curl_easy_init();
  curl_easy_setopt(curl, CURLOPT_URL, "http://data.commoncrawl.org/crawl-data/CC-MAIN-2017-04/segments/1484560279169.4/wet/CC-MAIN-20170116095119-00057-ip-10-171-10-70.ec2.internal.warc.wet.gz");
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, CurlCallback::FunctionPtr);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &callback);
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_perform(curl);
  curl_easy_cleanup(curl);
/*  while (std::cin.read(buf, 1024)) {
    stream.GiveBytes(buf, std::cin.gcount(), callback);
  }*/

}
