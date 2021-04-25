/* A buffered output stream.
 * The Writer class has this interface.
 * class Writer {
 *  private:
 *   void write(const void *data, size_t amount);
 *   void flush();
 * };
 */
#ifndef UTIL_BUFFERED_STREAM_H
#define UTIL_BUFFERED_STREAM_H

#include "util/fake_ostream.hh"
#include "util/file.hh"
#include "util/scoped.hh"

#include <cassert>
#include <cstring>

#include <stdint.h>

namespace util {

template <class Writer> class BufferedStream : public FakeOStream<BufferedStream<Writer> > {
  public:
    const std::size_t kBufferSize = 8192;
    template <typename... Args> explicit BufferedStream(Args&&... args)
      : buf_(util::MallocOrThrow(std::max<std::size_t>(kBufferSize, kToStringMaxBytes))),
        current_(static_cast<char*>(buf_.get())),
        end_(current_ + std::max<std::size_t>(kBufferSize, kToStringMaxBytes)),
        writer_(std::forward<Args>(args)...) {}

    /* The source of the move is left in an unusable state that can only be destroyed. */
#if __cplusplus >= 201103L
    BufferedStream(BufferedStream &&from) noexcept : buf_(std::move(from.buf_)), current_(from.current_), end_(from.end_) {
      from.end_ = reinterpret_cast<char*>(from.buf_.get());
      from.current_ = from.end_;
    }
#endif

    ~BufferedStream() {
      flush();
    }

    BufferedStream<Writer> &flush() {
      SpillBuffer();
      writer_.flush();
      return *this;
    }

    // For writes of arbitrary size.
    BufferedStream<Writer> &write(const void *data, std::size_t length) {
      if (UTIL_LIKELY(current_ + length <= end_)) {
        std::memcpy(current_, data, length);
        current_ += length;
        return *this;
      }
      SpillBuffer();
      if (current_ + length <= end_) {
        std::memcpy(current_, data, length);
        current_ += length;
      } else {
        writer_.write(data, length);
      }
      return *this;
    }

  private:
    friend class FakeOStream<BufferedStream<Writer> >;
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

    void SpillBuffer() {
      if (current_ != buf_.get()) {
        writer_.write(buf_.get(), current_ - (char*)buf_.get());
        current_ = static_cast<char*>(buf_.get());
      }
    }

    util::scoped_malloc buf_;
    char *current_, *end_;
    Writer writer_;
};

} // namespace

#endif
