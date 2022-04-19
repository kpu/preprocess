#include "xxhash.h"

#include "warcstream.hh"
#include "util/file_stream.hh"
#include "util/file_piece.hh"
#include "util/murmur_hash.hh"
#include "util/pool.hh"
#include "util/tokenize_piece.hh"

#include <charconv>
#include <exception>
#include <iostream>
#include <unordered_map>
#include <vector>

#include <curl/curl.h>
#include <string.h>

// Thrown for errors finding a match that can mostly r
class MatchException : public util::Exception {
  public:
    void SetLocation(const char *file, unsigned int line, const char *func, const char * /*child_name*/, const char *condition) {
      std::string old_text;
      what_.swap(old_text);
      what_ << file << ':' << line << '\t';
      if (func) what_ << func;
      what_ << '\t';
      if (condition) what_ << condition;
      what_ << '\t';
      what_ << old_text;
    }
};

struct Extract {
  // Raw line number in the WET
  uint64_t paragraph_number;
  // XXH3_64bits_withSeed of line in the WET
  uint64_t paragraph_digest;

  util::StringPiece original_line;
};

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

    void Clear() {
      map_.clear();
    }

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

    Iterator begin() { return map_.begin(); }
    Iterator end() { return map_.end(); }

  private:
    const std::vector<Extract> empty_;
    std::unordered_map<SHA1, std::vector<Extract> > map_;
};


class Output {
  public:
    explicit Output(const char *failure_log) : success_(1), failure_(util::CreateOrThrow(failure_log)) {}

    void Success(util::StringPiece original_line, util::StringPiece paragraph) {
      success_ << original_line << '\t' << paragraph << '\n';
    }
    void Failure(util::StringPiece original_line, util::StringPiece what) {
      failure_ << original_line << '\t' << what << '\n';
    }

  private:
    util::FileStream success_, failure_;
};

void Normalize(const util::StringPiece in, std::string &out) {
  // '|' goes to '_', '\t' goes to ' ', and '\r' to empty string.
  out.clear();
  for (char i : in) {
    switch (i) {
      case '|':
        out.push_back('_');
        break;
      case '\t':
        out.push_back(' ');
        break;
      case '\r':
        break;
      default:
        out.push_back(i);
    }
  }
}

bool ProcessExtract(const Extract &extract, util::StringPiece line, Output &out) {
  // First try with just the line as-is.
  XXH64_hash_t hash = XXH3_64bits_withSeed(line.data(), line.size(), 0);
  if (hash == extract.paragraph_digest) {
    out.Success(extract.original_line, line);
    return true;
  }
  // Then try normalizing the string.
  std::string normalized;
  Normalize(line, normalized);
  XXH64_hash_t norm_hash = XXH3_64bits_withSeed(normalized.data(), normalized.size(), 0);
  if (norm_hash == extract.paragraph_digest) {
    out.Success(extract.original_line, normalized);
    return true;
  }
  // Didn't match, let's fall back to matching regardless of line number.
  return false;
}

util::StringPiece Strip(const util::StringPiece &in) {
  util::StringPiece str(in);
  while (!str.empty() && util::kSpaces[(unsigned char)str[0]]) {
    str.remove_prefix(1);
  }
  while (!str.empty() && util::kSpaces[(unsigned char)str[str.size() - 1]]) {
    str.remove_suffix(1);
  }
  return str;
}

void FallbackHashTable(util::TokenIter<util::SingleCharacter, false> &line, std::vector<Extract>::const_iterator extract, std::vector<Extract>::const_iterator extract_end, Output &out) {
  std::unordered_multimap<uint64_t, const Extract*> lookup;
  for (; extract != extract_end; ++extract) {
    lookup.insert(std::pair<const uint64_t, const Extract*>(extract->paragraph_digest, &*extract));
  }
  std::string normalized;
  for (; line; ++line) {
    Normalize(*line, normalized);
    XXH64_hash_t norm_hash = XXH3_64bits_withSeed(normalized.data(), normalized.size(), 0);
    auto range = lookup.equal_range(norm_hash);
    for (auto i = range.first; i != range.second; ++i) {
      out.Success(i->second->original_line, normalized);
    }
    lookup.erase(range.first, range.second);
    if (lookup.empty()) return;
  }
  // Failed to match the lines in lookup.
  util::StringStream message;
  for (std::pair<const uint64_t, const Extract*> &entry : lookup) {
    message.clear();
    message << "Hash " << entry.first << " did not match any line in the WET";
    out.Failure(entry.second->original_line, message.str());
  }
}

void MatchLines(util::TokenIter<util::SingleCharacter, false> &line, const std::vector<Extract> &extracts, Output &out) {
  util::TokenIter<util::SingleCharacter, false> line_start(line);
  assert(!extracts.empty());
  uint64_t line_counter = 0;
  std::vector<Extract>::const_iterator extract = extracts.begin();
  for (; line; ++line) {
    // Upstream does python strip() then skips empty lines without counting them.
    util::StringPiece stripped = Strip(*line);
    if (stripped.empty()) continue;
    while (line_counter == extract->paragraph_number) {
      if (!ProcessExtract(*extract, stripped, out)) {
        // A line failed to match the expected hash.  Fall back to a hash join of all lines.
        FallbackHashTable(line_start, extract, extracts.end(), out);
        return;
      }
      if (++extract == extracts.end()) {
        return;
      }
    }
    ++line_counter;
  }
  UTIL_THROW(MatchException, "Paragraph number " << extract->paragraph_number << " exceeds size.")
}

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

// The WARC reader calls this for every document in the WARC.
class DocumentCallback {
  public:
    DocumentCallback(Retrieve &retrieve, Output &out) : retrieve_(retrieve), out_(out) {}

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
      if (it == retrieve_.end()) return;
      const std::vector<Extract> &extracts = it->second;
      assert(!extracts.empty());
      // Consume rest of the header.
      for (++line; ; ++line) {
        UTIL_THROW_IF(!line, MatchException, "Missing end of header");
        if (line->size() == 1 && (*line)[0] == '\r') break;
      }
      ++line; // Skip blank.
      MatchLines(line, extracts, out_);
      retrieve_.Success(it);
    }
  private:
    Retrieve &retrieve_;
    Output &out_;
};

class CurlCallback {
  public:
    CurlCallback(Retrieve &retrieve, Output &out) : document_(retrieve, out) {}

    size_t operator()(void *buffer, size_t length) {
      // As a C library, curl can't handle exceptions thrown by the callback.
      try {
        warc_.GiveBytes(static_cast<const char*>(buffer), length, document_);
      } catch (...) {
        exception_ = std::current_exception();
        return 0;
      }
      return length;
    }

    void CheckException() {
      if (exception_) {
        std::exception_ptr moved(std::move(exception_));
        std::rethrow_exception(moved);
      }
    }
  private:
    preprocess::WARCStream warc_;
    DocumentCallback document_;
    std::exception_ptr exception_;
};

class CurlWrap {
  public:
    CurlWrap() : curl_(curl_easy_init()) {
      UTIL_THROW_IF(!curl_, MatchException, "Failed to initialize CURL");
      UTIL_THROW_IF(CURLE_OK != curl_easy_setopt(curl_, CURLOPT_ERRORBUFFER, error_buffer_), MatchException, "CURL Setting error buffer failed");
      UTIL_THROW_IF(CURLE_OK != curl_easy_setopt(curl_, CURLOPT_FOLLOWLOCATION, 1L), MatchException, "CURL Setting follow location failed " << error_buffer_);
      UTIL_THROW_IF(CURLE_OK != curl_easy_setopt(curl_, CURLOPT_WRITEFUNCTION, FunctionPtr), MatchException, "CURL Setting function failed " << error_buffer_);
    }

    ~CurlWrap() {
      curl_easy_cleanup(curl_);
    }

    void Download(const char *url, CurlCallback &callback) {
      UTIL_THROW_IF(CURLE_OK != curl_easy_setopt(curl_, CURLOPT_URL, url), MatchException, "CURL Could not set URL " << error_buffer_);
      UTIL_THROW_IF(CURLE_OK != curl_easy_setopt(curl_, CURLOPT_WRITEDATA, &callback), MatchException, "CURL Could not set callback " << error_buffer_);
      CURLcode performed = curl_easy_perform(curl_);
      callback.CheckException();
      if (CURLE_OK == performed) return;
      UTIL_THROW(MatchException, "CURL perform failed " << error_buffer_);
    }

  private:
    static size_t FunctionPtr(void *buffer, size_t /* one */, size_t nmemb, void *ptr) {
      return (*static_cast<CurlCallback*>(ptr))(buffer, nmemb);
    }

    CURL *curl_;

    char error_buffer_[CURL_ERROR_SIZE];

};

void ParseLine(util::StringPiece line, util::StringPiece &wet_path, util::StringPiece &sha1, Extract &extract) {
  util::TokenIter<util::SingleCharacter, false> spaces(line, ' ');
  UTIL_THROW_IF2(!spaces, "Metadata missing");
  wet_path = *spaces;
  UTIL_THROW_IF2(!++spaces, "Metadata missing sha1");
  UTIL_THROW_IF2(!spaces->starts_with("sha1:"), "Expected hash starting with sha1");
  sha1 = *spaces;
  sha1.remove_prefix(5);
  UTIL_THROW_IF2(!++spaces, "Metadata missing URL");
  UTIL_THROW_IF2(!++spaces, "Metadata missing line");
  std::from_chars_result r = std::from_chars(spaces->data(), spaces->data() + spaces->size(), extract.paragraph_number, 10);
  UTIL_THROW_IF2(r.ec != std::errc(), "Error in number " << *spaces);
  UTIL_THROW_IF2(r.ptr != spaces->end(), "Did not consume full number " << *spaces);
  UTIL_THROW_IF2(!++spaces, "Metadata missing paragraph digest");
  r = std::from_chars(spaces->data(), spaces->end(), extract.paragraph_digest);
  UTIL_THROW_IF2(r.ec != std::errc(), "Error in number " << *spaces);
  UTIL_THROW_IF2(r.ptr != spaces->end(), "Did not consume full number " << *spaces);
}

void RunWARC(const char *url, CurlWrap &curl, Retrieve &retrieve, Output &out) {
  try {
    CurlCallback callback(retrieve, out);
    curl.Download(url, callback);
  } catch (const MatchException &e) {
    for (Retrieve::Iterator i = retrieve.begin(); i != retrieve.end(); ++i) {
      for (const Extract &extract : i->second) {
        out.Failure(extract.original_line, e.what());
      }
    }
    return;
  }
  for (Retrieve::Iterator i = retrieve.begin(); i != retrieve.end(); ++i) {
    for (const Extract &extract : i->second) {
      out.Failure(extract.original_line, "No error but unmatched");
    }
  }
}

void ProcessMetadata(const util::StringPiece download_prefix, util::FilePiece &in, Output &out) {
  Retrieve retrieve;
  util::Pool string_pool;
  CurlWrap curl;
  util::StringPiece previous_wet_path;
  std::string download_path(download_prefix.data(), download_prefix.size());
  for (util::StringPiece line : in) {
    util::StringPiece wet_path, sha1;
    Extract extract;
    ParseLine(line, wet_path, sha1, extract);
    if (wet_path != previous_wet_path) {
      // Flush existing data.
      if (!previous_wet_path.empty()) {
        download_path.replace(download_prefix.size(), download_path.size() - download_prefix.size(), previous_wet_path.data(), previous_wet_path.size());
        RunWARC(download_path.c_str(), curl, retrieve, out);
      }
      retrieve.Clear();
      string_pool.FreeAll();
    }
    // Store the full string line for use in printing later.
    extract.original_line = util::StringPiece(static_cast<const char*>(memcpy(string_pool.Allocate(line.size()), line.data(), line.size())), line.size());
    previous_wet_path = util::StringPiece(extract.original_line.data() + (wet_path.data() - line.data()), wet_path.size());
    retrieve.Add(sha1, extract);
  }
  if (!previous_wet_path.empty()) {
    download_path.replace(download_prefix.size(), download_path.size() - download_prefix.size(), previous_wet_path.data(), previous_wet_path.size());
    RunWARC(download_path.c_str(), curl, retrieve, out);
  }
}

int main() {
  util::FilePiece in(0, "stdin", &std::cerr, 4096);
  Output out("failure");
  ProcessMetadata("http://data.commoncrawl.org/", in, out);
}
