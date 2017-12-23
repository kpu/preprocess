#ifndef UTIL_OBJECT_POOL_H
#define UTIL_OBJECT_POOL_H

#include "util/fixed_array.hh"

#include <vector>

#include <stdint.h>

namespace util {

template <class T> class ObjectPool {
  public:
    ObjectPool() {}

    template <typename... Construct> T *Allocate(Construct... construct) {
      if (free_list_.empty() ||
          (free_list_.back().begin() + Capacity(free_list_.size()) == free_list_.back().end())) {
        free_list_.emplace_back(Capacity(free_list_.size() + 1));
      }
      free_list_.back().push_back(construct...);
      return &free_list_.back().back();
    }

    void FreeAll() {
      free_list_.clear();
    }

  private:
    static std::size_t Capacity(std::size_t index) {
      return 1ULL << index;
    }

    std::vector<util::FixedArray<T> > free_list_;
};

} // namespace util

#endif // UTIL_OBJECT_POOL_H
