#include "util/compress.hh"

#include "util/file.hh"
#include "util/have.hh"
#include "util/scoped.hh"

#include <algorithm>
#include <iostream>

#include <cassert>
#include <climits>
#include <cstdlib>
#include <cstring>

#ifdef HAVE_ZLIB
#include <zlib.h>
#endif

#ifdef HAVE_BZLIB
#include <bzlib.h>
#endif

#ifdef HAVE_XZLIB
#include <lzma.h>
#endif

namespace util {

CompressedException::CompressedException() throw() {}
CompressedException::~CompressedException() throw() {}

GZException::GZException() throw() {}
GZException::~GZException() throw() {}

BZException::BZException() throw() {}
BZException::~BZException() throw() {}

XZException::XZException() throw() {}
XZException::~XZException() throw() {}

void ReadBase::ReplaceThis(ReadBase *with, ReadCompressed &thunk) {
  thunk.internal_.reset(with);
}

ReadBase *ReadBase::Current(ReadCompressed &thunk) { return thunk.internal_.get(); }

uint64_t &ReadBase::ReadCount(ReadCompressed &thunk) {
  return thunk.raw_amount_;
}

namespace {

ReadBase *ReadFactory(int fd, uint64_t &raw_amount, const void *already_data, std::size_t already_size, bool require_compressed);

// Completed file that other classes can thunk to.
class Complete : public ReadBase {
  public:
    std::size_t Read(void *, std::size_t, ReadCompressed &) {
      return 0;
    }
};

class Uncompressed : public ReadBase {
  public:
    explicit Uncompressed(int fd) : fd_(fd) {}

    std::size_t Read(void *to, std::size_t amount, ReadCompressed &thunk) {
      std::size_t got = PartialRead(fd_.get(), to, amount);
      ReadCount(thunk) += got;
      return got;
    }

  private:
    scoped_fd fd_;
};

class UncompressedWithHeader : public ReadBase {
  public:
    UncompressedWithHeader(int fd, const void *already_data, std::size_t already_size) : fd_(fd) {
      assert(already_size);
      buf_.reset(malloc(already_size));
      if (!buf_.get()) throw std::bad_alloc();
      memcpy(buf_.get(), already_data, already_size);
      remain_ = static_cast<uint8_t*>(buf_.get());
      end_ = remain_ + already_size;
    }

    std::size_t Read(void *to, std::size_t amount, ReadCompressed &thunk) {
      assert(buf_.get());
      assert(remain_ != end_);
      std::size_t sending = std::min<std::size_t>(amount, end_ - remain_);
      memcpy(to, remain_, sending);
      remain_ += sending;
      if (remain_ == end_) {
        ReplaceThis(new Uncompressed(fd_.release()), thunk);
      }
      return sending;
    }

  private:
    scoped_malloc buf_;
    uint8_t *remain_;
    uint8_t *end_;

    scoped_fd fd_;
};

static const std::size_t kInputBuffer = 16384;

template <class Compression> class ReadStream : public ReadBase {
  public:
    ReadStream(int fd, const void *already_data, std::size_t already_size)
      : file_(fd),
        in_buffer_(MallocOrThrow(kInputBuffer)),
        back_(memcpy(in_buffer_.get(), already_data, already_size), already_size) {}

    std::size_t Read(void *to, std::size_t amount, ReadCompressed &thunk) {
      if (amount == 0) return 0;
      back_.SetOutput(to, amount);
      do {
        if (!back_.AvailInput()) ReadInput(thunk);
        if (!back_.Process()) {
          // reached end, at least for the compressed portion.
          std::size_t ret = back_.NextOutput() - static_cast<const uint8_t*>(to);
          ReplaceThis(ReadFactory(file_.release(), ReadCount(thunk), back_.NextInput(), back_.AvailInput(), true), thunk);
          if (ret) return ret;
          // We did not read anything this round, so clients might think EOF.  Transfer responsibility to the next reader.
          return Current(thunk)->Read(to, amount, thunk);
        }
      } while (back_.NextOutput() == to);
      return back_.NextOutput() - static_cast<const uint8_t*>(to);
    }

  private:
    void ReadInput(ReadCompressed &thunk) {
      assert(!back_.AvailInput());
      std::size_t got = ReadOrEOF(file_.get(), in_buffer_.get(), kInputBuffer);
      back_.SetInput(in_buffer_.get(), got);
      ReadCount(thunk) += got;
    }

    scoped_fd file_;
    scoped_malloc in_buffer_;

    Compression back_;
};

#ifdef HAVE_ZLIB
class GZip {
  public:
    static const std::size_t kSizeMax;

    GZip() {
      stream_.zalloc = Z_NULL;
      stream_.zfree = Z_NULL;
      stream_.opaque = Z_NULL;
      stream_.msg = NULL;
    }

    void SetOutput(void *to, std::size_t amount) {
      stream_.next_out = static_cast<Bytef*>(to);
      stream_.avail_out = std::min<std::size_t>(kSizeMax, amount);
    }

    void SetInput(const void *base, std::size_t amount) {
      assert(amount <= kSizeMax);
      stream_.next_in = const_cast<Bytef*>(static_cast<const Bytef*>(base));
      stream_.avail_in = amount;
    }

    const uint8_t *NextInput() const {
      return reinterpret_cast<const uint8_t*>(stream_.next_in);
    }

    const uint8_t *NextOutput() const {
      return reinterpret_cast<const uint8_t*>(stream_.next_out);
    }

    std::size_t AvailInput() const { return stream_.avail_in; }
    std::size_t AvailOutput() const { return stream_.avail_out; }

  protected:
    z_stream stream_;
};

const std::size_t GZip::kSizeMax = static_cast<std::size_t>(std::numeric_limits<uInt>::max());

class GZipRead : public GZip {
  public:
    GZipRead(const void *base, std::size_t amount) {
      SetInput(base, amount);
      // 32 for zlib and gzip decoding with automatic header detection.
      // 15 for maximum window size.
      UTIL_THROW_IF(Z_OK != inflateInit2(&stream_, 32 + 15), GZException, "Failed to initialize zlib.");
    }

    ~GZipRead() {
      if (Z_OK != inflateEnd(&stream_)) {
        std::cerr << "zlib could not close properly." << std::endl;
        abort();
      }
    }

    bool Process() {
      int result = inflate(&stream_, 0);
      switch (result) {
        case Z_OK:
          return true;
        case Z_STREAM_END:
          return false;
        case Z_ERRNO:
          UTIL_THROW(ErrnoException, "zlib error");
        default:
          UTIL_THROW(GZException, "zlib encountered " << (stream_.msg ? stream_.msg : "an error ") << " code " << result);
      }
    }
};

} // namespace

class GZipWrite : public GZip {
  public:
    explicit GZipWrite(int level) {
      UTIL_THROW_IF(Z_OK != deflateInit2(
            &stream_,
            level,
            Z_DEFLATED,
            16 /* gzip support */ + 15 /* default window */,
            8 /* default */,
            Z_DEFAULT_STRATEGY), GZException, "Failed to initialize zlib decompression.");
    }

    ~GZipWrite() {
      deflateEnd(&stream_);
    }

    void Reset() {
      UTIL_THROW_IF(Z_OK != deflateReset(&stream_), GZException, "Trying to reset");
    }

    static const std::size_t kMinOutput;

    bool EnoughOutput() const {
      return AvailOutput() >= kMinOutput;
    }

    void Process() {
      int result = deflate(&stream_, Z_NO_FLUSH);
      UTIL_THROW_IF(Z_OK != result, GZException, "zlib encountered " << (stream_.msg ? stream_.msg : "an error ") << " code " << result);
    }

    bool Finish() {
      assert(stream_.avail_out);
      int result = deflate(&stream_, Z_FINISH);
      switch (result) {
        case Z_STREAM_END:
          return true;
        case Z_OK:
          return false;
        // "If deflate returns with Z_OK or Z_BUF_ERROR, this function must be called again with Z_FINISH and more output space"
        case Z_BUF_ERROR:
          return false;
        default:
          UTIL_THROW(GZException, "zlib encountered " << (stream_.msg ? stream_.msg : "an error ") << " code " << result);
      }
    }
};

const std::size_t GZipWrite::kMinOutput = 6; /* magic number in zlib.h to avoid multiple ends */

namespace {
#endif // HAVE_ZLIB

#ifdef HAVE_BZLIB
class BZip {
  public:
    static const std::size_t kSizeMax;
    BZip() {
      memset(&stream_, 0, sizeof(stream_));
    }

    void SetOutput(void *base, std::size_t amount) {
      stream_.next_out = static_cast<char*>(base);
      stream_.avail_out = std::min<std::size_t>(std::numeric_limits<unsigned int>::max(), amount);
    }

    void SetInput(const void *base, std::size_t amount) {
      stream_.next_in = const_cast<char*>(static_cast<const char*>(base));
      stream_.avail_in = amount;
    }

    const uint8_t *NextOutput() const {
      return reinterpret_cast<const uint8_t*>(stream_.next_out);
    }

    const uint8_t *NextInput() const {
      return reinterpret_cast<const uint8_t*>(stream_.next_in);
    }

    std::size_t AvailInput() const { return stream_.avail_in; }
    std::size_t AvailOutput() const { return stream_.avail_out; }

  protected:
    void HandleError(int value) {
      switch(value) {
        case BZ_OK:
          return;
        case BZ_RUN_OK:
          return;
        case BZ_CONFIG_ERROR:
          UTIL_THROW(BZException, "bzip2 seems to be miscompiled.");
        case BZ_PARAM_ERROR:
          UTIL_THROW(BZException, "bzip2 Parameter error");
        case BZ_DATA_ERROR:
          UTIL_THROW(BZException, "bzip2 detected a corrupt file");
        case BZ_DATA_ERROR_MAGIC:
          UTIL_THROW(BZException, "bzip2 detected bad magic bytes.  Perhaps this was not a bzip2 file after all?");
        case BZ_MEM_ERROR:
          throw std::bad_alloc();
        default:
          UTIL_THROW(BZException, "Unknown bzip2 error code " << value);
      }
    }

    bz_stream stream_;
};

const std::size_t BZip::kSizeMax = static_cast<std::size_t>(std::numeric_limits<unsigned int>::max());

class BZipRead : public BZip {
  public:
    BZipRead(const void *base, std::size_t amount) : BZip() {
      SetInput(base, amount);
      HandleError(BZ2_bzDecompressInit(&stream_, 0, 0));
    }

    ~BZipRead() {
      try {
        HandleError(BZ2_bzDecompressEnd(&stream_));
      } catch (const std::exception &e) {
        std::cerr << e.what() << std::endl;
        abort();
      }
    }

    bool Process() {
      int ret = BZ2_bzDecompress(&stream_);
      if (ret == BZ_STREAM_END) return false;
      HandleError(ret);
      return true;
    }
};

class BZipWrite : public BZip {
  public:
    explicit BZipWrite(int level) : level_(level) {
      level_ = std::max<int>(level_, 1);
      level_ = std::min<int>(level_, 9);
      HandleError(BZ2_bzCompressInit(&stream_, level_, 0, 0));
    }

    ~BZipWrite() {
      try {
        HandleError(BZ2_bzCompressEnd(&stream_));
      } catch (const std::exception &e) {
        std::cerr << e.what() << std::endl;
        abort();
      }
    }

    static const std::size_t kMinOutput;

    bool EnoughOutput() const {
      return AvailOutput() >= kMinOutput;
    }

    void Process() {
      HandleError(BZ2_bzCompress(&stream_, BZ_RUN));
    }

    // TODO flushing state
    bool Finish() {
      assert(stream_.avail_out);
      int result = BZ2_bzCompress(&stream_, BZ_FINISH);
      switch(result) {
        case BZ_STREAM_END:
          return true;
        case BZ_FINISH_OK:
          return false;
        default:
          HandleError(result);
          UTIL_THROW(BZException, "Confused by bzip result " << result);
      }
    }

    // TODO flushing state
    void Reset() {
      HandleError(BZ2_bzCompressEnd(&stream_));
      HandleError(BZ2_bzCompressInit(&stream_, level_, 0, 0));
    }

  private:
    int level_;
};

const std::size_t BZipWrite::kMinOutput = 1; /* seemingly no magic number? */

#endif // HAVE_BZLIB

#ifdef HAVE_XZLIB
class XZip {
  public:
    XZip(const void *base, std::size_t amount)
      : stream_(), action_(LZMA_RUN) {
      memset(&stream_, 0, sizeof(stream_));
      SetInput(base, amount);
      HandleError(lzma_stream_decoder(&stream_, UINT64_MAX, 0));
    }

    ~XZip() {
      lzma_end(&stream_);
    }

    void SetOutput(void *base, std::size_t amount) {
      stream_.next_out = static_cast<uint8_t*>(base);
      stream_.avail_out = amount;
    }

    void SetInput(const void *base, std::size_t amount) {
      stream_.next_in = static_cast<const uint8_t*>(base);
      stream_.avail_in = amount;
      if (!amount) action_ = LZMA_FINISH;
    }

    bool Process() {
      lzma_ret status = lzma_code(&stream_, action_);
      if (status == LZMA_STREAM_END) return false;
      HandleError(status);
      return true;
    }

    const uint8_t *NextOutput() const {
      return reinterpret_cast<const uint8_t*>(stream_.next_out);
    }

    const uint8_t *NextInput() const {
      return reinterpret_cast<const uint8_t*>(stream_.next_in);
    }

    std::size_t AvailInput() const { return stream_.avail_in; }
    std::size_t AvailOutput() const { return stream_.avail_out; }

  private:
    void HandleError(lzma_ret value) {
      switch (value) {
        case LZMA_OK:
          return;
        case LZMA_MEM_ERROR:
          throw std::bad_alloc();
        case LZMA_FORMAT_ERROR:
          UTIL_THROW(XZException, "xzlib says file format not recognized");
        case LZMA_OPTIONS_ERROR:
          UTIL_THROW(XZException, "xzlib says unsupported compression options");
        case LZMA_DATA_ERROR:
          UTIL_THROW(XZException, "xzlib says this file is corrupt");
        case LZMA_BUF_ERROR:
          UTIL_THROW(XZException, "xzlib says unexpected end of input");
        default:
          UTIL_THROW(XZException, "unrecognized xzlib error " << value);
      }
    }

    lzma_stream stream_;
    lzma_action action_;
};
#endif // HAVE_XZLIB

class IStreamReader : public ReadBase {
  public:
    explicit IStreamReader(std::istream &stream) : stream_(stream) {}

    std::size_t Read(void *to, std::size_t amount, ReadCompressed &thunk) {
      if (!stream_.read(static_cast<char*>(to), amount)) {
        UTIL_THROW_IF(!stream_.eof(), ErrnoException, "istream error");
        amount = stream_.gcount();
      }
      ReadCount(thunk) += amount;
      return amount;
    }

  private:
    std::istream &stream_;
};

enum MagicResult {
  UTIL_UNKNOWN, UTIL_GZIP, UTIL_BZIP, UTIL_XZIP
};

MagicResult DetectMagic(const void *from_void, std::size_t length) {
  const uint8_t *header = static_cast<const uint8_t*>(from_void);
  if (length >= 2 && header[0] == 0x1f && header[1] == 0x8b) {
    return UTIL_GZIP;
  }
  const uint8_t kBZMagic[3] = {'B', 'Z', 'h'};
  if (length >= sizeof(kBZMagic) && !memcmp(header, kBZMagic, sizeof(kBZMagic))) {
    return UTIL_BZIP;
  }
  const uint8_t kXZMagic[6] = { 0xFD, '7', 'z', 'X', 'Z', 0x00 };
  if (length >= sizeof(kXZMagic) && !memcmp(header, kXZMagic, sizeof(kXZMagic))) {
    return UTIL_XZIP;
  }
  return UTIL_UNKNOWN;
}

ReadBase *ReadFactory(int fd, uint64_t &raw_amount, const void *already_data, const std::size_t already_size, bool require_compressed) {
  scoped_fd hold(fd);
  std::string header(reinterpret_cast<const char*>(already_data), already_size);
  if (header.size() < ReadCompressed::kMagicSize) {
    std::size_t original = header.size();
    header.resize(ReadCompressed::kMagicSize);
    std::size_t got = ReadOrEOF(fd, &header[original], ReadCompressed::kMagicSize - original);
    raw_amount += got;
    header.resize(original + got);
  }
  if (header.empty()) {
    return new Complete();
  }
  switch (DetectMagic(&header[0], header.size())) {
    case UTIL_GZIP:
#ifdef HAVE_ZLIB
      return new ReadStream<GZipRead>(hold.release(), header.data(), header.size());
#else
      UTIL_THROW(CompressedException, "This looks like a gzip file but gzip support was not compiled in.");
#endif
    case UTIL_BZIP:
#ifdef HAVE_BZLIB
      return new ReadStream<BZipRead>(hold.release(), &header[0], header.size());
#else
      UTIL_THROW(CompressedException, "This looks like a bzip file (it begins with BZh), but bzip support was not compiled in.");
#endif
    case UTIL_XZIP:
#ifdef HAVE_XZLIB
      return new ReadStream<XZip>(hold.release(), header.data(), header.size());
#else
      UTIL_THROW(CompressedException, "This looks like an xz file, but xz support was not compiled in.");
#endif
    default:
      UTIL_THROW_IF(require_compressed, CompressedException, "Uncompressed data detected after a compresssed file.  This could be supported but usually indicates an error.");
      return new UncompressedWithHeader(hold.release(), header.data(), header.size());
  }
}

} // namespace

bool ReadCompressed::DetectCompressedMagic(const void *from_void) {
  return DetectMagic(from_void, kMagicSize) != UTIL_UNKNOWN;
}

ReadCompressed::ReadCompressed(int fd) {
  Reset(fd);
}

ReadCompressed::ReadCompressed(std::istream &in) {
  Reset(in);
}

ReadCompressed::ReadCompressed() {}

void ReadCompressed::Reset(int fd) {
  raw_amount_ = 0;
  internal_.reset();
  internal_.reset(ReadFactory(fd, raw_amount_, NULL, 0, false));
}

void ReadCompressed::Reset(std::istream &in) {
  internal_.reset();
  internal_.reset(new IStreamReader(in));
}

std::size_t ReadCompressed::Read(void *to, std::size_t amount) {
  return internal_->Read(to, amount, *this);
}

std::size_t ReadCompressed::ReadOrEOF(void *const to_in, std::size_t amount) {
  uint8_t *to = reinterpret_cast<uint8_t*>(to_in);
  while (amount) {
    std::size_t got = Read(to, amount);
    if (!got) break;
    to += got;
    amount -= got;
  }
  return to - reinterpret_cast<uint8_t*>(to_in);
}

WriteBase::WriteBase() {}
WriteBase::~WriteBase() {}

template <class Compressor> class WriteStream : public WriteBase {
  public:
    WriteStream(int out, int level = 9, std::size_t compressed_buffer = 4096)
      : buf_size_(std::max<std::size_t>(Compressor::kMinOutput, compressed_buffer)),
        buf_(buf_size_),
        writer_(out),
        dirty_(true /* Even if input is empty, generate a valid gzip file */),
        compressor_(level) {
      compressor_.SetOutput(buf_.get(), buf_size_);
    }

    void write(const void *data_void, std::size_t amount) {
      const uint8_t *data = static_cast<const uint8_t*>(data_void);
      /* Guard against maximum size */
      for (; amount > Compressor::kSizeMax; data += Compressor::kSizeMax, amount -= Compressor::kSizeMax) {
        write(data, Compressor::kSizeMax);
      }

      compressor_.SetInput(data, amount);
      while (compressor_.AvailInput()) {
        if (!compressor_.EnoughOutput()) {
          writer_.write(buf_.get(), compressor_.NextOutput() - reinterpret_cast<const uint8_t*>(buf_.get()));
          compressor_.SetOutput(buf_.get(), buf_size_);
        }
        compressor_.Process();
      }
      dirty_ = true;
    }  

    void flush() {
      if (!dirty_) return;
      do {
        if (!compressor_.EnoughOutput()) {
          writer_.write(buf_.get(), compressor_.NextOutput() - reinterpret_cast<const uint8_t*>(buf_.get()));
          compressor_.SetOutput(buf_.get(), buf_size_);
        }
      } while (!compressor_.Finish());
      if (compressor_.NextOutput() != buf_.get()) {
        writer_.write(buf_.get(), compressor_.NextOutput() - reinterpret_cast<const uint8_t*>(buf_.get()));
      }
      writer_.flush();
      compressor_.Reset();
      compressor_.SetOutput(buf_.get(), buf_size_);
      dirty_ = false; /* No need for an empty gzip after the first one */
    }


  private:
    // Holding compressed data.
    std::size_t buf_size_;
    scoped_malloc buf_;

    // TODO: generic Writer backend.
    FileWriter writer_;

    bool dirty_; // Do we have stuff to write to the file with flush?

    Compressor compressor_;
};

class WriteUncompressed : public WriteBase {
  public:
    explicit WriteUncompressed(int out) : writer_(out) {}

    void write(const void *data_void, std::size_t amount) {
      writer_.write(data_void, amount);
    }

    void flush() { writer_.flush(); }

  private:
    FileWriter writer_;
};

WriteCompressed::WriteCompressed(int fd, WriteCompressed::Compression compression) {
  switch (compression) {
    case NONE:
      backend_.reset(new WriteUncompressed(fd));
      return;
    case GZIP:
#ifdef HAVE_ZLIB
      backend_.reset(new WriteStream<GZipWrite>(fd));
#else
      UTIL_THROW(CompressedException, "gzip support not compiled in");
#endif
      return;
    case BZIP:
#ifdef HAVE_BZLIB
      backend_.reset(new WriteStream<BZipWrite>(fd));
#else
      UTIL_THROW(CompressedException, "bzip support not compiled in");
#endif
      return;
    case XZIP:
      UTIL_THROW(CompressedException, "xzip writing not implemented yet");
  }
}

WriteCompressed::~WriteCompressed() {
  flush();
}

void WriteCompressed::write(const void *data_void, std::size_t amount) {
  backend_->write(data_void, amount);
}

void WriteCompressed::flush() {
  backend_->flush();
}

#ifdef HAVE_ZLIB
namespace {

void EnsureOutput(GZipWrite &writer, std::string &to) {
  const std::size_t kIncrement = 4096;
  if (!writer.EnoughOutput()) {
    std::size_t old_done = writer.NextOutput() - reinterpret_cast<const uint8_t*>(to.data());
    to.resize(to.size() + kIncrement);
    writer.SetOutput(&to[old_done], to.size() - old_done);
  }
}

} // namespace

void GZCompress(StringPiece from, std::string &to, int level) {
  to.clear();
  to.resize(4096);
  GZipWrite writer(level);
  writer.SetInput(from.data(), from.size());
  writer.SetOutput(&to[0], to.size());
  while (!writer.EnoughOutput()) {
    EnsureOutput(writer, to);
    writer.Process();
  }
  do {
    EnsureOutput(writer, to);
  } while (!writer.Finish());
  to.resize(writer.NextOutput() - reinterpret_cast<const uint8_t*>(to.data()));
}
#else
void GZCompress(StringPiece, std::string &, int) {
  UTIL_THROW(CompressedException, "gzip not compiled in");
}
#endif

} // namespace util
