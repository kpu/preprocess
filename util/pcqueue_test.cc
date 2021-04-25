#include "util/pcqueue.hh"

#define BOOST_TEST_MODULE PCQueueTest
#include <boost/test/unit_test.hpp>

#include <thread>

namespace util {
namespace {

BOOST_AUTO_TEST_CASE(SingleThread) {
  PCQueue<int> queue(10);
  for (int i = 0; i < 10; ++i) {
    queue.Produce(i);
  }
  for (int i = 0; i < 10; ++i) {
    BOOST_CHECK_EQUAL(i, queue.Consume());
  }
}

BOOST_AUTO_TEST_CASE(SingleInSingleOut) {
  PCQueue<int> queue(15);
  std::thread writer([&queue]() {
      for (int i = 0; i < 100; ++i) {
        queue.Produce(i);
      }
  });
  for (int i = 0; i < 100; ++i) {
    BOOST_CHECK_EQUAL(i, queue.Consume());
  }
  writer.join();
}

void MultipleWriters() {
  const unsigned kCount = 2000;
  const unsigned kNumThreads = 4;
  PCQueue<unsigned> queue(13);
  auto writer = [&queue, kCount]() {
    for (unsigned i = 0; i < kCount; ++i) {
      queue.Produce(i);
    }
  };
  std::vector<std::thread> threads;
  for (unsigned i = 0; i < kNumThreads; ++i) {
    threads.emplace_back(writer);
  }
  unsigned seen[kCount] = {0};
  for (unsigned i = 0; i < kCount * kNumThreads; ++i) {
    unsigned got = queue.Consume();
    BOOST_CHECK_LT(got, kCount);
    seen[got]++;
    // Since each thread generates in order, counts should be monotonically non-increasing.
    BOOST_CHECK(!got || seen[got] <= seen[got - 1]);
  }
  for (unsigned i = 0; i < kCount; ++i) {
    BOOST_CHECK_EQUAL(seen[i], kNumThreads);
  }
  for (std::thread &t : threads) {
    t.join();
  }
}

}
} // namespace util
