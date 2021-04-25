/* A buffered output stream.
 * The Writer class has this interface.
 * class Writer {
 *  private:
 *   void write(const void *data, size_t amount);
 *   void flush();
 * };
 */
#ifndef UTIL_THREADED_BUFFERED_STREAM_H
#define UTIL_THREADED_BUFFERED_STREAM_H

#include "util/fake_ostream.hh"
#include "util/file.hh"
#include "util/fixed_array.hh"
#include "util/pcqueue.hh" // Semaphore
#include "util/scoped.hh"

#include <cassert>
#include <cstring>
#include <thread>

#include <stdint.h>

namespace util {

/* Single reader, single writer queue of blocks */
class BlockQueue {
  private:
    struct Block {
      char *base;
      std::size_t size;
    };
  public:
    static const size_t kBlocks = 3;
    // std::max isn't constexpr?
    static const std::size_t kBlockSize = (8192 > kToStringMaxBytes) ? 8192 : kToStringMaxBytes;

    class Lease {
      public:
        Lease(Block *circular, Block *&iterator, Semaphore &success, Semaphore &failure)
        : current_(iterator), circular_(circular), success_(success), failure_(failure) {
          failure_.wait();
        }

        ~Lease() {
          // Check we're not a dead move.
          if (circular_) {
            failure_.post();
          }
        }

        Lease(Lease &) = delete;
        Lease &operator=(Lease &) = delete;

        // Make the old one do nothing.
        Lease(Lease &&from)
          : current_(from.current_), circular_(from.circular_), success_(from.success_), failure_(from.failure_) {
          from.current_ = nullptr;
          from.circular_ = nullptr;
        }

        void SuccessNext() {
          if (++current_ == circular_ + kBlocks) {
            current_ = circular_;
          }
          success_.post();
          failure_.wait();
        }

        char *Base() const { return current_->base; }

        const std::size_t Size() const { return current_->size; }

        std::size_t &Size() { return current_->size; }

      private:
        Block *current_;

        Block *circular_;

        Semaphore &success_, &failure_;
    };

    BlockQueue() : all_(kBlockSize * kBlocks), output_it_(circular_), trash_it_(circular_), output_(0), trash_(kBlocks) {
      for (std::size_t i = 0; i < kBlocks; ++i) {
        circular_[i].base = static_cast<char*>(all_.get()) + i * kBlockSize;
        circular_[i].size = kBlockSize;
      }
    }

    BlockQueue(BlockQueue &from) = delete;

    Lease In() {
      return Lease(circular_, output_it_, output_, trash_);
    }

    Lease Out() {
      return Lease(circular_, trash_it_, trash_, output_);
    }

  private:
    scoped_malloc all_;

    Block circular_[kBlocks];

    // Indices into circular buffer.
    Block *output_it_;
    Block *trash_it_;

    Semaphore output_;
    Semaphore trash_;
};

template <class Writer> class ThreadedBufferedStream : public FakeOStream<ThreadedBufferedStream<Writer> > {
  public:
    template <typename... Args> explicit ThreadedBufferedStream(Args&&... args)
      : queue_(), lease_(queue_.In()),
        current_(lease_.Base()),
        end_(current_ + BlockQueue::kBlockSize),
        writer_(std::forward<Args>(args)...) {
      thread_ = std::thread([this]() {
          for (BlockQueue::Lease lease(queue_.Out()); lease.Size(); lease.SuccessNext()) {
            writer_.write(lease.Base(), lease.Size());
          }
          writer_.flush();
        });
    }

    ~ThreadedBufferedStream() {
      SpillBuffer();
      // Poison.
      lease_.Size() = 0;
      lease_.SuccessNext();
      thread_.join();
    }

    // No flush.

    // For writes of arbitrary size.  We have to copy to the buffer even for big stuff.
    ThreadedBufferedStream<Writer> &write(const void *data_void, std::size_t length) {
      const char *data = static_cast<const char*>(data_void);
      while (UTIL_UNLIKELY(current_ + length > end_)) {
        std::memcpy(current_, data, end_ - current_);
        data += end_ - current_;
        length -= (end_ - current_);
        current_ = end_;
        SpillBuffer();
      }
      std::memcpy(current_, data, length);
      current_ += length;
      return *this;
    }

  private:
    friend class FakeOStream<ThreadedBufferedStream<Writer> >;
    // For writes directly to buffer guaranteed to have amount < buffer size.
    char *Ensure(std::size_t amount) {
      if (UTIL_UNLIKELY(current_ + amount > end_)) {
        SpillBuffer();
        assert(current_ + amount <= end_);
      }
      return current_;
    }

    void AdvanceTo(char *to) {
      current_ = to;
      assert(current_ <= end_);
    }

    void SpillBuffer() {
      if (current_ == lease_.Base()) return;
      lease_.Size() = current_ - lease_.Base();
      lease_.SuccessNext();
      current_ = lease_.Base();
      end_ = current_ + BlockQueue::kBlockSize;
    }

    BlockQueue queue_;

    BlockQueue::Lease lease_;

    char *current_, *end_;

    Writer writer_;

    std::thread thread_;
};

} // namespace util

#endif
