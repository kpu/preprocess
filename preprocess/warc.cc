#include "preprocess/warc.hh"

#include "util/exception.hh"
#include "util/file.hh"
#include "util/compress.hh"

#include <cstdlib>
#include <limits>
#include <string>
#include <strings.h>

namespace preprocess {

bool ReadMore(util::ReadCompressed &reader, std::string &out) {
  const std::size_t kRead = 4096;
  std::size_t had = out.size();
  out.resize(out.size() + kRead);
  std::size_t got = reader.Read(&out[had], out.size() - had);
  if (!got) {
    // End of file
    UTIL_THROW_IF(had, util::EndOfFileException, "Unexpected end of file inside header");
    return false;
  }
  out.resize(had + got);
  return true;
}

class HeaderReader {
  public:
    HeaderReader(util::ReadCompressed &reader, std::string &out)
      : reader_(reader), out_(out), consumed_(0) {}

    bool Line(StringPiece &line) {
      std::size_t newline_start = consumed_;
      std::size_t newline;
      while (std::string::npos == (newline = out_.find('\n', newline_start))) {
        newline_start = out_.size();
        if (!ReadMore(reader_, out_)) return false;
      }
      // The line is [consumed, newline).  A blank line indicates header end.
      line = StringPiece(out_.data() + consumed_, newline - consumed_);
      // Remove carriage return if present.
      if (!line.empty() && line.data()[line.size() - 1] == '\r') {
        line = StringPiece(line.data(), line.size() - 1);
      }
      consumed_ = newline + 1;
      return true;
    }

    std::size_t Consumed() const { return consumed_; }

  private:
    util::ReadCompressed &reader_;
    std::string &out_;

    std::size_t consumed_;
};

bool WARCReader::Read(std::string &out) {
  std::swap(overhang_, out);
  overhang_.clear();
  out.reserve(32768);
  HeaderReader header(reader_, out);
  StringPiece line;
  if (!header.Line(line)) return false;
  UTIL_THROW_IF(line != "WARC/1.0", util::Exception, "Expected WARC/1.0 header but got `" << line << '\'');
  std::size_t length = 0;
  bool seen_content_length = false;
  const char kContentLength[] = "Content-Length:";
  const std::size_t kContentLengthLength = sizeof(kContentLength) - 1;
  while (!line.empty()) {
    UTIL_THROW_IF(!header.Line(line), util::EndOfFileException, "WARC ended in header.");
    if (line.size() >= kContentLengthLength && !strncasecmp(line.data(), kContentLength, kContentLengthLength)) {
      UTIL_THROW_IF2(seen_content_length, "Two Content-Length headers?");
      seen_content_length = true;
      char *end;
      length = std::strtoll(line.data() + kContentLengthLength, &end, 10);
      // TODO: tolerate whitespace?
      UTIL_THROW_IF2(end != line.data() + line.size(), "Content-Length parse error in `" << line << '\'');
    }
  }
  UTIL_THROW_IF2(!seen_content_length, "No Content-Length: header in " << out);
  std::size_t total_length = header.Consumed() + length + 4 /* CRLF CRLF after data as specified in the standard. */;

  if (total_length < out.size()) {
    overhang_.assign(out.data() + total_length, out.size() - total_length);
    out.resize(total_length);
  } else {
    std::size_t start = out.size();
    out.resize(total_length);
    while (start != out.size()) {
      std::size_t got = reader_.Read(&out[start], out.size() - start);
      UTIL_THROW_IF(!got, util::EndOfFileException, "Unexpected end of file while reading content of length " << length);
      start += got;
    }
  }
  // Check CRLF CRLF.
  UTIL_THROW_IF2(StringPiece(out.data() + out.size() - 4, 4) != StringPiece("\r\n\r\n", 4), "End of WARC record missing CRLF CRLF");
  return true;
}

} // namespace preprocess
