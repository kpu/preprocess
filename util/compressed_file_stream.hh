#ifndef UTIL_COMPRESSED_FILE_STREAM_H
#define UTIL_COMPRESSED_FILE_STREAM_H

#include "util/fake_ostream.hh"
#include "util/file.hh"
#include "util/scoped.hh"
#include "util/compress.hh"

#include <memory>
#include <cassert>
#include <cstring>

#include <stdint.h>

namespace util {

class Compressor {
public:
  virtual ~Compressor() = 0;

  virtual void SetOutput(void *to, std::size_t amount) = 0;
  virtual void SetInput(const void *base, std::size_t amount) = 0;
  virtual const void* GetOutput() const = 0;

  virtual void Process() = 0;
  virtual bool HasInput() const = 0;
  virtual bool OutOfSpace() const = 0;
  virtual bool Finish() = 0;
};

inline Compressor::~Compressor() {
  // Pure virtual destructor, still needs implementation.
}

class CompressedFileStream : public FakeOStream<CompressedFileStream> {
  public:
    explicit CompressedFileStream(std::unique_ptr<Compressor> compressor, int out = -1, std::size_t buffer_size = 8192)
      : compressor_(std::move(compressor)),
        buf_(util::MallocOrThrow(std::max<std::size_t>(buffer_size, kToStringMaxBytes))),
        compressed_(util::MallocOrThrow(std::max<std::size_t>(buffer_size, kToStringMaxBytes))),
        compressed_size_(std::max<std::size_t>(buffer_size, kToStringMaxBytes)),
        current_(static_cast<char*>(buf_.get())),
        end_(current_ + std::max<std::size_t>(buffer_size, kToStringMaxBytes)),
        fd_(out)
        {
          compressor_->SetOutput(compressed_.get(), compressed_size_);
        }

    // <missing move constructor>

    ~CompressedFileStream() {
      finish();
    }

    void SetFD(int to) {
      finish();
      fd_ = to;
    }

    CompressedFileStream &finish() {
      flush();
      FinishCompressed();
      return *this;
    }

    CompressedFileStream &flush() {
      if (current_ != buf_.get()) {
        WriteCompressed(buf_.get(), current_ - (char*) buf_.get());
        current_ = static_cast<char*>(buf_.get());
      }
      return *this;
    }

    // For writes of arbitrary size.
    CompressedFileStream &write(const void *data, std::size_t length) {
      if (UTIL_LIKELY(current_ + length <= end_)) {
        std::memcpy(current_, data, length);
        current_ += length;
        return *this;
      }
      flush();
      if (current_ + length <= end_) {
        std::memcpy(current_, data, length);
        current_ += length;
      } else {
        WriteCompressed(data, length);
      }
      return *this;
    }

  protected:
    friend class FakeOStream<CompressedFileStream>;
    // For writes directly to buffer guaranteed to have amount < buffer size.
    char *Ensure(std::size_t amount) {
      if (UTIL_UNLIKELY(current_ + amount > end_)) {
        flush();
        assert(current_ + amount <= end_);
      }
      return current_;
    }

    void AdvanceTo(char *to) {
      current_ = to;
      assert(current_ <= end_);
    }

  private:
    void WriteCompressed(const void *data, std::size_t length) {
      compressor_->SetInput(data, length);
      while (compressor_->HasInput()) {
        if (compressor_->OutOfSpace())
          FlushCompressed();
        compressor_->Process();
      }
    }

    void FlushCompressed() {
      std::size_t compressed_len = reinterpret_cast<const char*>(compressor_->GetOutput()) - reinterpret_cast<const char*>(compressed_.get());
      util::WriteOrThrow(fd_, compressed_.get(), compressed_len);
      compressor_->SetOutput(compressed_.get(), compressed_size_);
    }

    void FinishCompressed() {
      // Generate last bit of compressed output
      do {
        FlushCompressed();
      } while (!compressor_->Finish());
      // Write ending
      FlushCompressed();
    }
    
    util::scoped_malloc buf_, compressed_;
    std::size_t compressed_size_;
    char *current_, *end_;
    int fd_;
    std::unique_ptr<Compressor> compressor_;
};

} // namespace

#endif
