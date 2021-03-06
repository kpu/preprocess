#ifndef UTIL_COMPRESS_H
#define UTIL_COMPRESS_H

#include "util/exception.hh"
#include "util/file.hh"
#include "util/scoped.hh"

#include <cstddef>
#include <stdint.h>
#include <string>

namespace util {

class CompressedException : public Exception {
  public:
    CompressedException() throw();
    virtual ~CompressedException() throw();
};

class GZException : public CompressedException {
  public:
    GZException() throw();
    ~GZException() throw();
};

class BZException : public CompressedException {
  public:
    BZException() throw();
    ~BZException() throw();
};

class XZException : public CompressedException {
  public:
    XZException() throw();
    ~XZException() throw();
};

class ReadCompressed;

class ReadBase {
  public:
    virtual ~ReadBase() {}

    virtual std::size_t Read(void *to, std::size_t amount, ReadCompressed &thunk) = 0;

  protected:
    static void ReplaceThis(ReadBase *with, ReadCompressed &thunk);

    ReadBase *Current(ReadCompressed &thunk);

    static uint64_t &ReadCount(ReadCompressed &thunk);
};

class ReadCompressed {
  public:
    static const std::size_t kMagicSize = 6;
    // Must have at least kMagicSize bytes.
    static bool DetectCompressedMagic(const void *from);

    // Takes ownership of fd.
    explicit ReadCompressed(int fd);

    // Try to avoid using this.  Use the fd instead.
    // There is no decompression support for istreams.
    explicit ReadCompressed(std::istream &in);

    // Must call Reset later.
    ReadCompressed();

    // Takes ownership of fd.
    void Reset(int fd);

    // Same advice as the constructor.
    void Reset(std::istream &in);

    std::size_t Read(void *to, std::size_t amount);

    // Repeatedly call read to fill a buffer unless EOF is hit.
    // Return number of bytes read.
    std::size_t ReadOrEOF(void *const to, std::size_t amount);

    uint64_t RawAmount() const { return raw_amount_; }

  private:
    friend class ReadBase;

    scoped_ptr<ReadBase> internal_;

    uint64_t raw_amount_;
};

class WriteBase {
  public:
    virtual ~WriteBase();

    virtual void write(const void *data, std::size_t amount) = 0;

    virtual void flush() = 0;
 
  protected:
    WriteBase();
};

/* Currently xzip is missing */
class WriteCompressed {
  public:
    enum Compression { NONE, GZIP, BZIP, XZIP };
    // Takes ownership of fd.
    explicit WriteCompressed(int fd, Compression compression);

    ~WriteCompressed();

    void write(const void *data, std::size_t amount);

    void flush();

  private:
    scoped_ptr<WriteBase> backend_;
};

// Very basic gzip compression support.  Normally this would involve streams
// but I needed the compression in the thread with fused output.
void GZCompress(StringPiece from, std::string &to, int level = 9);

} // namespace util

#endif // UTIL_COMPRESS_H
