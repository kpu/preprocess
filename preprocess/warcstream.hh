#pragma once
#include "../util/exception.hh"
#include "zlib.h"
#include <string>
#include <iostream>

namespace preprocess {

class WARCStream {
  public:
    WARCStream() {
      z_.zalloc = nullptr;
      z_.zfree = nullptr;
      z_.opaque = nullptr;
      z_.avail_in = 0;
      z_.next_in = nullptr;
      UTIL_THROW_IF2(inflateInit2(&z_, 32) != Z_OK, "zlib inflateInit2 failed");
    }

    ~WARCStream() {
      if (inflateEnd(&z_) != Z_OK) {
        std::cerr << "inflateEnd failed" << std::endl;
      }
    }

    template <class Callback> void GiveBytes(const char *data, std::size_t size, Callback &callback) {
      z_.avail_in = size;
      // zlib shouldn't be messing with input
      z_.next_in = const_cast<Bytef*>(reinterpret_cast<const Bytef*>(data));
      const std::size_t kGrow = 4096;
      while (z_.avail_in) {
        std::size_t scratch_start = document_.size();
        document_.resize(scratch_start + kGrow);
        z_.next_out = reinterpret_cast<Bytef*>(&document_[scratch_start]);
        z_.avail_out = kGrow;
        int ret = inflate(&z_, Z_NO_FLUSH);
        UTIL_THROW_IF2(ret != Z_OK && ret != Z_STREAM_END, "zlib inflate returned unexpected code " << ret);
        document_.resize(reinterpret_cast<char*>(z_.next_out) - document_.data());
        if (ret == Z_OK) {
          continue;
        }
        callback(document_);
        document_.clear();
        UTIL_THROW_IF2(inflateReset(&z_) != Z_OK, "zlib inflateReset failed");
      }
    }

  private:
    z_stream z_;
    std::string document_;
};

} // namespace preprocess
